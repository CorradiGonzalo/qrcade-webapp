import "server-only";

/**
 * Cliente de la API de Mercado Pago — vive enteramente en el backend.
 * Antes esto lo hacía la ESP32 directo (y se colgaba); ahora es Next.js
 * quien arma la Tienda/Caja, crea la orden de cobro, y consulta el
 * resultado. La ESP sólo recibe el resultado final vía /api/device/checkin.
 *
 * ─────────────────────────────────────────────────────────────────────
 * MIGRACIÓN (sept. 2026): esto usaba la API "legacy" de QR de Mercado
 * Pago (PUT /instore/qr/seller/collectors/{user_id}/pos/{external_pos_id}
 * /orders). Un pago real se quedó colgado en "Un momento, el cobro se
 * está registrando" y nunca avanzó — investigando, la doc de MP dice
 * textualmente que esa API legacy "will discontinue" y que "si estás
 * integrando por primera vez, usá la nueva Orders API". Esto reescribe
 * todo el módulo para usar esa Orders API (confirmado contra la
 * referencia oficial, no sólo la guía):
 *   - Tienda: SIN CAMBIOS (POST/GET /users/{user_id}/stores...).
 *   - Caja/POS: POST/GET /v2/pos (antes /pos), con
 *     X-Idempotency-Key obligatorio y config.qr.operating_mode="pdv" en
 *     vez del viejo `fixed_amount: true` suelto.
 *   - "Bloquear precio fijo": ya no es un PUT idempotente sobre la
 *     caja — ahora se CREA una orden (POST /v1/orders, type "qr",
 *     config.qr.mode "static") con expiration_time largo para que no
 *     haga falta re-crearla todo el tiempo; igual la volvemos a crear
 *     después de cada venta (mismo patrón que antes).
 *   - Notificaciones: YA NO hay `notification_url` por-orden — Mercado
 *     Pago lo sacó de esta API. El webhook se configura UNA VEZ en el
 *     panel (Tus integraciones → tu app → Webhooks → evento "Order").
 *     Avisale al dueño que tiene que cargarlo ahí a mano.
 *   - Modo "combo" (monto abierto): la nueva Orders API no tiene un
 *     equivalente 1:1 confirmado todavía al viejo DELETE que liberaba
 *     el monto — quedó en la función `abrirMontoQR` usando el endpoint
 *     legacy de mejor esfuerzo (la doc de MP dice que el legacy sigue
 *     funcionando por ahora, sólo que no para integraciones nuevas).
 *     Ninguna placa en producción usa "combo" todavía, así que se
 *     prioriza dejar bien el modo "fijo" (el que sí está en uso) y
 *     el modo combo queda pendiente de una segunda pasada.
 * ─────────────────────────────────────────────────────────────────────
 */

const MP_BASE = "https://api.mercadopago.com";
const TIMEOUT_MS = 8000;

export class MercadoPagoError extends Error {
  status?: number;
  code?: string;
  constructor(message: string, status?: number, code?: string) {
    super(message);
    this.name = "MercadoPagoError";
    this.status = status;
    this.code = code;
  }
}

async function mpFetch(
  token: string,
  path: string,
  init: { method?: string; body?: unknown; headers?: Record<string, string> } = {}
): Promise<unknown> {
  const controller = new AbortController();
  const timeout = setTimeout(() => controller.abort(), TIMEOUT_MS);

  try {
    const res = await fetch(`${MP_BASE}${path}`, {
      method: init.method ?? "GET",
      headers: {
        Authorization: `Bearer ${token}`,
        "Content-Type": "application/json",
        ...init.headers,
      },
      body: init.body !== undefined ? JSON.stringify(init.body) : undefined,
      signal: controller.signal,
    });

    const text = await res.text();
    const data = text ? JSON.parse(text) : null;

    if (!res.ok) {
      const errObj = data && typeof data === "object" ? (data as Record<string, unknown>) : null;
      const msg =
        (errObj && "message" in errObj ? String(errObj.message) : null) ??
        `Mercado Pago respondió ${res.status}`;
      const code = errObj && "error" in errObj ? String(errObj.error) : undefined;
      throw new MercadoPagoError(msg, res.status, code);
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

/** Header X-Idempotency-Key: una clave nueva por operación (UUID v4). */
function idemKey(): string {
  return crypto.randomUUID();
}

function esperar(ms: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, ms));
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

/** Busca la tienda por external_id; si no existe, la crea. (Sin cambios en la migración.) */
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

/**
 * Busca la caja (POS) por external_id; si no existe, la crea. Devuelve el
 * QR estático (string EMV crudo) — el mismo QR de siempre para esa caja,
 * el monto que cobra lo define la orden activa (ver crearOrdenMontoFijo).
 *
 * MIGRADO a POST/GET /v2/pos (antes /pos): requiere X-Idempotency-Key,
 * y el modo pasa a vivir en config.qr.operating_mode ("pdv" = atendido,
 * el mismo comportamiento que el viejo `fixed_amount: true`).
 */
export async function buscarOCrearCaja(
  token: string,
  storeId: string,
  externalIdCaja: string,
  nombreCaja: string
): Promise<string> {
  let busqueda: { data?: Array<{ qr_response?: { qr_code?: string } }> } | null = null;
  try {
    busqueda = (await mpFetch(
      token,
      `/v2/pos?external_id=${encodeURIComponent(externalIdCaja)}`
    )) as { data?: Array<{ qr_response?: { qr_code?: string } }> };
  } catch (err) {
    if (!(err instanceof MercadoPagoError && err.status === 404)) throw err;
  }

  if (busqueda?.data && busqueda.data.length > 0) {
    const qr = busqueda.data[0].qr_response?.qr_code;
    if (qr) return qr;
  }

  // Cuando la tienda se acaba de crear, la API de MP a veces tarda un
  // instante en propagarla internamente y /v2/pos devuelve "store_not_found"
  // aunque la tienda exista — reintentamos unas pocas veces antes de darnos
  // por vencidos en lugar de hacer fallar todo el guardado.
  const REINTENTOS = 3;
  let ultimoError: unknown;

  const bodyEnviado = {
    name: nombreCaja,
    store_id: storeId,
    external_id: externalIdCaja,
    config: {
      qr: {
        operating_mode: "pdv",
        category: 621102, // Gastronomía — la categoría más cercana a "vending"
      },
    },
  };

  for (let intento = 1; intento <= REINTENTOS; intento++) {
    try {
      const creada = (await mpFetch(token, "/v2/pos", {
        method: "POST",
        headers: { "X-Idempotency-Key": idemKey() },
        body: bodyEnviado,
      })) as { qr_response?: { qr_code?: string } };

      const qr = creada?.qr_response?.qr_code;
      if (!qr) {
        throw new MercadoPagoError("Mercado Pago no devolvió el QR de la caja.");
      }
      return qr;
    } catch (err) {
      ultimoError = err;
      const esStoreNotFound =
        err instanceof MercadoPagoError &&
        (err.status === 404 || /store.?not.?found/i.test(err.message));
      if (!esStoreNotFound || intento === REINTENTOS) break;
      await esperar(1500 * intento);
    }
  }

  if (ultimoError instanceof MercadoPagoError) throw ultimoError;
  throw new MercadoPagoError("No se pudo crear la caja en Mercado Pago.");
}

// Duración larga para que la orden de "monto fijo" no haga falta
// re-crearla constantemente — el máximo que permite la API es 3600
// horas (150 días). Aun así la re-creamos después de CADA venta (ver
// webhook), así que esto es sólo un colchón para el caso raro de una
// placa sin ninguna venta en meses.
const EXPIRACION_ORDEN_LARGA = "PT3600H";

/**
 * Crea la orden que fija el monto que cobra el QR estático de una caja
 * — 1 pago = 1 ficha. Reemplaza al viejo "bloquearPrecioFijo" (que era
 * un PUT idempotente sobre la caja; acá cada llamada CREA una orden
 * nueva, que es como funciona la Orders API).
 */
export async function crearOrdenMontoFijo(
  token: string,
  externalIdCaja: string,
  titulo: string,
  monto: number
): Promise<void> {
  const montoStr = monto.toFixed(2);
  await mpFetch(token, "/v1/orders", {
    method: "POST",
    headers: { "X-Idempotency-Key": idemKey() },
    body: {
      type: "qr",
      total_amount: montoStr,
      description: titulo,
      // Usamos el external_id de la caja también como external_reference
      // de la orden — así el webhook puede matchear la placa igual que
      // antes (device.pos_id === orden.external_reference).
      external_reference: externalIdCaja,
      expiration_time: EXPIRACION_ORDEN_LARGA,
      config: {
        qr: {
          external_pos_id: externalIdCaja,
          mode: "static",
        },
      },
      transactions: {
        payments: [{ amount: montoStr }],
      },
      items: [
        {
          title: titulo,
          unit_price: montoStr,
          quantity: 1,
          unit_measure: "unit",
        },
      ],
    },
  });
}

/**
 * Saca cualquier monto fijo — el QR vuelve a aceptar un monto abierto
 * (modo combos).
 *
 * OJO: esto TODAVÍA usa el endpoint legacy (DELETE .../instore/qr/...).
 * Mercado Pago dice que el legacy sigue funcionando por ahora (sólo que
 * no lo recomiendan para integraciones nuevas) y no hay, por ahora, un
 * reemplazo 1:1 confirmado en la nueva Orders API para "liberar" una
 * caja a monto abierto. Ninguna placa usa "combo" en producción
 * todavía — si llegás a activar una, probala con un pago real de
 * monto abierto antes de confiar en que este QR funciona.
 */
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

export interface MpOrder {
  id: string;
  status: string;
  totalAmount: number;
  externalReference: string | null;
  /** id del pago interno de la orden (para dedupe) — puede venir null si la orden no tiene transacciones todavía. */
  paymentId: string | null;
}

/**
 * Trae el estado actual de una orden. El webhook de Mercado Pago sólo
 * manda un aviso liviano (básicamente el id) — como hacíamos antes con
 * los pagos, siempre volvemos a pedir el recurso completo en vez de
 * confiar en lo que venga en el payload del webhook.
 */
export async function obtenerOrden(token: string, orderId: string): Promise<MpOrder> {
  const data = (await mpFetch(token, `/v1/orders/${orderId}`)) as {
    id: string;
    status: string;
    total_amount?: string | number;
    external_reference?: string | null;
    transactions?: { payments?: Array<{ id?: string }> };
  };
  return {
    id: data.id,
    status: data.status,
    totalAmount: Number(data.total_amount ?? 0),
    externalReference: data.external_reference ?? null,
    paymentId: data.transactions?.payments?.[0]?.id ?? null,
  };
}

/** Genera un identificador corto y estable a partir del id (uuid) de la placa. */
export function externalIdBase(deviceId: string): string {
  return "QRC" + soloAlfanumerico(deviceId).slice(0, 12).toUpperCase();
}
