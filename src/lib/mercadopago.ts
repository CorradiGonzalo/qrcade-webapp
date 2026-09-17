import "server-only";

/**
 * Cliente de la API de Mercado Pago — vive enteramente en el backend.
 * Antes esto lo hacía la ESP32 directo (y se colgaba); ahora es Next.js
 * quien arma la Tienda/Caja, bloquea o abre el monto del QR, y consulta
 * pagos. La ESP sólo recibe el resultado final vía /api/device/checkin.
 */

const MP_BASE = "https://api.mercadopago.com";
const TIMEOUT_MS = 8000;

// Dominio público de la app, para armar la URL de webhook que le pasamos
// a Mercado Pago por orden. Se puede pisar con una env var si algún día
// hay que apuntar a otro dominio (staging, etc.).
const WEBHOOK_BASE_URL = process.env.NEXT_PUBLIC_APP_URL ?? "https://qrcade.dev";

export class MercadoPagoError extends Error {
  status?: number;
  constructor(message: string, status?: number) {
    super(message);
    this.name = "MercadoPagoError";
    this.status = status;
  }
}

async function mpFetch(
  token: string,
  path: string,
  init: { method?: string; body?: unknown } = {}
): Promise<unknown> {
  const controller = new AbortController();
  const timeout = setTimeout(() => controller.abort(), TIMEOUT_MS);

  try {
    const res = await fetch(`${MP_BASE}${path}`, {
      method: init.method ?? "GET",
      headers: {
        Authorization: `Bearer ${token}`,
        "Content-Type": "application/json",
      },
      body: init.body !== undefined ? JSON.stringify(init.body) : undefined,
      signal: controller.signal,
    });

    const text = await res.text();
    const data = text ? JSON.parse(text) : null;

    if (!res.ok) {
      const msg =
        (data && typeof data === "object" && "message" in data
          ? String((data as Record<string, unknown>).message)
          : null) ?? `Mercado Pago respondió ${res.status}`;
      throw new MercadoPagoError(msg, res.status);
    }

    return data;
  } catch (err) {
    if (err instanceof MercadoPagoError) throw err;
    if (err instanceof Error && err.name === "AbortError") {
      throw new MercadoPagoError("Mercado Pago no respondió a tiempo.");
    }
    throw new MercadoPagoError(
      err instanceof Error ? err.message : "Error desconocido llamando a Mercado Pago."
    );
  } finally {
    clearTimeout(timeout);
  }
}

export function sanitizar(s: string): string {
  return s.toLowerCase().replace(/\s+/g, "-");
}

export function soloAlfanumerico(s: string): string {
  return s.replace(/[^a-zA-Z0-9]/g, "");
}

/** Valida el token y devuelve el user_id de la cuenta de MP. */
export async function obtenerUserId(token: string): Promise<string> {
  const data = (await mpFetch(token, "/users/me")) as { id?: number | string };
  if (!data?.id) throw new MercadoPagoError("El token no devolvió un user_id válido.");
  return String(data.id);
}

/** Busca la tienda por external_id; si no existe, la crea. */
export async function buscarOCrearTienda(
  token: string,
  userId: string,
  externalIdTienda: string,
  nombreLocal: string
): Promise<string> {
  // OJO: si no existe NINGUNA tienda con ese external_id, este endpoint de
  // MP no devuelve 200 con resultados vacíos — devuelve 404 directamente.
  // Un 404 acá es un resultado válido ("no existe todavía"), no un error:
  // lo tratamos como búsqueda vacía y seguimos a crearla.
  let busqueda: { results?: Array<{ id: number | string }> } | null = null;
  try {
    busqueda = (await mpFetch(
      token,
      `/users/${userId}/stores/search?external_id=${encodeURIComponent(externalIdTienda)}`
    )) as { results?: Array<{ id: number | string }> };
  } catch (err) {
    if (!(err instanceof MercadoPagoError && err.status === 404)) throw err;
  }

  if (busqueda?.results && busqueda.results.length > 0) {
    return String(busqueda.results[0].id);
  }

  const creada = (await mpFetch(token, `/users/${userId}/stores`, {
    method: "POST",
    body: {
      name: nombreLocal,
      external_id: externalIdTienda,
      location: {
        street_name: "Sin especificar",
        street_number: "0",
        city_name: "Córdoba",
        state_name: "Córdoba",
        latitude: -31.4201,
        longitude: -64.1888,
        reference: "",
        zip_code: "5000",
      },
    },
  })) as { id?: number | string };

  if (!creada?.id) throw new MercadoPagoError("No se pudo crear la tienda en Mercado Pago.");
  return String(creada.id);
}

function esperar(ms: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

/** Busca la caja (POS) por external_id; si no existe, la crea. Devuelve el QR dinámico. */
export async function buscarOCrearCaja(
  token: string,
  storeId: string,
  externalIdCaja: string,
  nombreCaja: string
): Promise<string> {
  let busqueda: { results?: Array<{ qr_code?: string }> } | null = null;
  try {
    busqueda = (await mpFetch(
      token,
      `/pos?external_id=${encodeURIComponent(externalIdCaja)}`
    )) as { results?: Array<{ qr_code?: string }> };
  } catch (err) {
    if (!(err instanceof MercadoPagoError && err.status === 404)) throw err;
  }

  if (busqueda?.results && busqueda.results.length > 0) {
    const qr = busqueda.results[0].qr_code;
    if (qr) return qr;
  }

  // Cuando la tienda se acaba de crear, la API de MP a veces tarda un
  // instante en propagarla internamente y /pos devuelve "Store not found"
  // aunque la tienda exista — reintentamos unas pocas veces antes de darnos
  // por vencidos en lugar de hacer fallar todo el guardado.
  const REINTENTOS = 3;
  let ultimoError: unknown;

  const bodyEnviado = {
    name: nombreCaja,
    fixed_amount: true,
    store_id: Number(storeId),
    external_id: externalIdCaja,
    category: 621102,
  };

  for (let intento = 1; intento <= REINTENTOS; intento++) {
    try {
      const creada = (await mpFetch(token, "/pos", {
        method: "POST",
        body: bodyEnviado,
      })) as { qr_code?: string };

      if (!creada?.qr_code) {
        throw new MercadoPagoError("Mercado Pago no devolvió el QR de la caja.");
      }
      return creada.qr_code;
    } catch (err) {
      ultimoError = err;
      const esStoreNotFound =
        err instanceof MercadoPagoError && /store not found/i.test(err.message);
      if (!esStoreNotFound || intento === REINTENTOS) break;
      await esperar(1500 * intento);
    }
  }

  if (ultimoError instanceof MercadoPagoError) throw ultimoError;
  throw new MercadoPagoError("No se pudo crear la caja en Mercado Pago.");
}

/** Bloquea el QR a un monto fijo — 1 pago = 1 ficha. */
export async function bloquearPrecioFijo(
  token: string,
  userId: string,
  externalIdCaja: string,
  titulo: string,
  monto: number
): Promise<void> {
  await mpFetch(
    token,
    `/instore/qr/seller/collectors/${userId}/pos/${externalIdCaja}/orders`,
    {
      method: "PUT",
      body: {
        external_reference: externalIdCaja,
        title: titulo,
        description: titulo,
        total_amount: monto,
        // La API de instore-orders deja pegar la notificación acá mismo,
        // por orden — así en modo "monto fijo" no dependemos de que el
        // dueño configure el webhook a mano en su cuenta de MP.
        notification_url: `${WEBHOOK_BASE_URL}/api/mp/webhook`,
        items: [
          {
            title: titulo,
            unit_price: monto,
            quantity: 1,
            unit_measure: "unit",
            total_amount: monto,
          },
        ],
      },
    }
  );
}

/** Saca cualquier monto fijo — el QR vuelve a aceptar un monto abierto (modo combos). */
export async function abrirMontoQR(
  token: string,
  userId: string,
  externalIdCaja: string
): Promise<void> {
  try {
    await mpFetch(
      token,
      `/instore/qr/seller/collectors/${userId}/pos/${externalIdCaja}/orders`,
      { method: "DELETE" }
    );
  } catch (err) {
    // Si no había ninguna orden fija cargada, MP devuelve error — no pasa nada.
    if (!(err instanceof MercadoPagoError)) throw err;
  }
}

export interface MpPayment {
  id: string;
  status: string;
  transaction_amount: number;
  external_reference: string | null;
}

export async function obtenerPago(token: string, paymentId: string): Promise<MpPayment> {
  const data = (await mpFetch(token, `/v1/payments/${paymentId}`)) as {
    id: number | string;
    status: string;
    transaction_amount: number;
    external_reference?: string | null;
  };
  return {
    id: String(data.id),
    status: data.status,
    transaction_amount: data.transaction_amount,
    external_reference: data.external_reference ?? null,
  };
}

/** Genera un identificador corto y estable a partir del id (uuid) de la placa. */
export function externalIdBase(deviceId: string): string {
  return "QRC" + soloAlfanumerico(deviceId).slice(0, 12).toUpperCase();
}
