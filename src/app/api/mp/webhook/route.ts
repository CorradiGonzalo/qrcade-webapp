import { NextResponse } from "next/server";
import { createAdminClient } from "@/lib/supabase/admin";
import { MercadoPagoError, crearOrdenMontoFijo, obtenerOrden } from "@/lib/mercadopago";
import { logDeviceEvent } from "@/lib/device-events";
import type { Device, Profile } from "@/lib/supabase/types";

/**
 * Webhook de Mercado Pago. Cada dueño tiene que cargar ESTA MISMA url en
 * su propia cuenta de MP — pero OJO, desde la migración a la Orders API
 * (ver mercadopago.ts) esto ya NO se hace por-orden con un campo
 * `notification_url` como antes: hay que entrar a Tus integraciones →
 * la app del dueño → Webhooks → Configurar notificaciones, pegar esta
 * URL y tildar el evento "Order (Mercado Pago)". Sin ese paso manual,
 * MP nunca llama acá — no hay forma de hacerlo por código.
 *
 * Como el token de cada uno es distinto, usamos el `user_id` que MP
 * manda en la notificación para saber de qué dueño es la orden y con
 * qué token consultarla.
 *
 * OJO formato del payload: esto está basado en la documentación de MP
 * para el evento "order" de la nueva Orders API (no se pudo verificar
 * contra un webhook real todavía — el primero que llegue de verdad hay
 * que revisarlo). Por eso logueamos el body completo de cualquier
 * notificación con un `type` que no sea "order", para poder ajustar
 * rápido si el nombre real del topic o la forma del payload es distinta.
 */
export async function POST(request: Request) {
  const body = await request.json().catch(() => null);

  const topic = body?.type ?? body?.topic;
  const orderId = body?.data?.id ?? null;
  const mpUserId = body?.user_id ? String(body.user_id) : null;

  // Siempre 200 salvo error nuestro: MP reintenta agresivamente si no.
  if (topic !== "order") {
    if (topic) {
      console.warn(
        `Webhook de MP: topic "${topic}" no reconocido (se esperaba "order"). Body completo:`,
        JSON.stringify(body)
      );
    }
    return NextResponse.json({ ok: true, ignored: true });
  }

  if (!orderId) {
    console.warn("Webhook de MP: evento 'order' sin data.id. Body completo:", JSON.stringify(body));
    return NextResponse.json({ ok: true, ignored: true });
  }

  const admin = createAdminClient();

  if (!mpUserId) {
    console.warn("Webhook de MP sin user_id, no se puede identificar al dueño.", body);
    return NextResponse.json({ ok: true, ignored: true });
  }

  const { data: profile } = await admin
    .from("profiles")
    .select("id, mp_access_token, mp_user_id")
    .eq("mp_user_id", mpUserId)
    .maybeSingle<Pick<Profile, "id" | "mp_access_token" | "mp_user_id">>();

  if (!profile?.mp_access_token) {
    console.warn("Webhook de MP: no hay dueño con mp_user_id=" + mpUserId);
    return NextResponse.json({ ok: true, ignored: true });
  }

  let orden;
  try {
    orden = await obtenerOrden(profile.mp_access_token, String(orderId));
  } catch (err) {
    const msg = err instanceof MercadoPagoError ? err.message : "error desconocido";
    console.error("Webhook de MP: no se pudo consultar la orden " + orderId + ": " + msg);
    return NextResponse.json({ ok: true, error: "no se pudo consultar la orden" });
  }

  // "processed" = orden pagada con éxito. Cualquier otro estado (created,
  // canceled, expired, refunded) no acredita nada.
  if (orden.status !== "processed" || !orden.externalReference) {
    return NextResponse.json({ ok: true, ignored: true });
  }

  // Dedupe: MP puede reenviar la misma notificación varias veces. Usamos
  // el id del pago interno de la orden (o, si no vino, el id de la orden)
  // como clave — mismo campo/columna que antes (mp_payments.mp_payment_id).
  const dedupeId = orden.paymentId ?? orden.id;

  const { data: yaProcesado } = await admin
    .from("mp_payments")
    .select("id")
    .eq("mp_payment_id", dedupeId)
    .maybeSingle();

  if (yaProcesado) {
    return NextResponse.json({ ok: true, already_processed: true });
  }

  const { data: device } = await admin
    .from("devices")
    .select("*")
    .eq("owner_id", profile.id)
    .eq("pos_id", orden.externalReference)
    .single<Device>();

  if (!device) {
    console.warn(
      "Webhook de MP: orden pagada sin placa que matchee pos_id=" + orden.externalReference
    );
    await admin.from("mp_payments").insert({
      device_id: null,
      mp_payment_id: dedupeId,
      amount: orden.totalAmount,
      fichas_dispensed: 0,
      status: "sin_placa",
    });
    return NextResponse.json({ ok: true, ignored: true });
  }

  let fichas = 0;

  if (device.mode === "fijo") {
    // La orden se creó con el monto fijo, así que debería coincidir
    // siempre — igual lo confirmamos antes de tirar la ficha.
    if (device.mp_monto_fijo && orden.totalAmount === Number(device.mp_monto_fijo)) {
      fichas = 1;
    }
  } else {
    const { data: combo } = await admin
      .from("device_ficha_combos")
      .select("fichas")
      .eq("device_id", device.id)
      .eq("monto", orden.totalAmount)
      .maybeSingle();
    fichas = combo?.fichas ?? 0;
  }

  if (fichas > 0) {
    await admin.from("device_commands").insert({
      device_id: device.id,
      command: `dispense:${fichas}`,
    });
    await logDeviceEvent(
      admin,
      device.id,
      "dispense_payment",
      `Pago acreditado de $${orden.totalAmount} — tirando ${fichas} ficha(s).`
    );
  } else {
    console.warn(
      `Webhook de MP: pago de $${orden.totalAmount} en placa ${device.id} sin combo que coincida — no se acredita nada.`
    );
  }

  await admin.from("mp_payments").insert({
    device_id: device.id,
    mp_payment_id: dedupeId,
    amount: orden.totalAmount,
    fichas_dispensed: fichas,
    status: fichas > 0 ? "acreditado" : "sin_match",
  });

  // En modo fijo, creamos una orden nueva para la próxima venta — cada
  // orden de la Orders API se consume con el pago, no queda "reusable"
  // como el viejo bloqueo por PUT.
  if (device.mode === "fijo" && device.mp_monto_fijo && device.pos_id) {
    try {
      await crearOrdenMontoFijo(
        profile.mp_access_token,
        device.pos_id,
        device.caja_name ?? "QRcade",
        Number(device.mp_monto_fijo)
      );
    } catch (err) {
      console.error("No se pudo re-armar la orden de monto fijo tras el pago:", err);
    }
  }

  return NextResponse.json({ ok: true, fichas_dispensed: fichas });
}
