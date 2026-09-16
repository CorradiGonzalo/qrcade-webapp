import { NextResponse } from "next/server";
import { createAdminClient } from "@/lib/supabase/admin";
import { MercadoPagoError, bloquearPrecioFijo, obtenerPago } from "@/lib/mercadopago";
import type { Device, Profile } from "@/lib/supabase/types";

/**
 * Webhook de Mercado Pago. Cada dueño configura ESTA MISMA url en su
 * propia cuenta de MP (Tu negocio → Webhooks) para que le avise acá
 * cuando entra un pago. Como el token de cada uno es distinto, usamos
 * el `user_id` que MP manda en la notificación para saber de qué dueño
 * es el pago y con qué token consultarlo.
 *
 * No requiere sesión — se valida yendo a buscar el pago a la API de MP
 * con el token guardado del dueño dueño de ese user_id; si no hay match
 * no se hace nada.
 */
export async function POST(request: Request) {
  const url = new URL(request.url);
  const body = await request.json().catch(() => null);

  const topic = body?.type ?? body?.topic ?? url.searchParams.get("topic");
  const paymentId =
    body?.data?.id ?? body?.resource ?? url.searchParams.get("id") ?? null;
  const mpUserId = body?.user_id ? String(body.user_id) : null;

  // Siempre 200 salvo error nuestro: MP reintenta agresivamente si no.
  if (topic !== "payment" || !paymentId) {
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

  let pago;
  try {
    pago = await obtenerPago(profile.mp_access_token, String(paymentId));
  } catch (err) {
    const msg = err instanceof MercadoPagoError ? err.message : "error desconocido";
    console.error("Webhook de MP: no se pudo consultar el pago " + paymentId + ": " + msg);
    return NextResponse.json({ ok: true, error: "no se pudo consultar el pago" });
  }

  if (pago.status !== "approved" || !pago.external_reference) {
    return NextResponse.json({ ok: true, ignored: true });
  }

  // Dedupe: MP puede reenviar la misma notificación varias veces.
  const { data: yaProcesado } = await admin
    .from("mp_payments")
    .select("id")
    .eq("mp_payment_id", pago.id)
    .maybeSingle();

  if (yaProcesado) {
    return NextResponse.json({ ok: true, already_processed: true });
  }

  const { data: device } = await admin
    .from("devices")
    .select("*")
    .eq("owner_id", profile.id)
    .eq("pos_id", pago.external_reference)
    .single<Device>();

  if (!device) {
    console.warn(
      "Webhook de MP: pago aprobado sin placa que matchee pos_id=" + pago.external_reference
    );
    await admin.from("mp_payments").insert({
      device_id: null,
      mp_payment_id: pago.id,
      amount: pago.transaction_amount,
      fichas_dispensed: 0,
      status: "sin_placa",
    });
    return NextResponse.json({ ok: true, ignored: true });
  }

  let fichas = 0;

  if (device.mode === "fijo") {
    // El QR está bloqueado a mp_monto_fijo, así que el pago debería
    // coincidir siempre — igual lo confirmamos antes de tirar la ficha.
    if (device.mp_monto_fijo && Number(pago.transaction_amount) === Number(device.mp_monto_fijo)) {
      fichas = 1;
    }
  } else {
    const { data: combo } = await admin
      .from("device_ficha_combos")
      .select("fichas")
      .eq("device_id", device.id)
      .eq("monto", pago.transaction_amount)
      .maybeSingle();
    fichas = combo?.fichas ?? 0;
  }

  if (fichas > 0) {
    await admin.from("device_commands").insert({
      device_id: device.id,
      command: `dispense:${fichas}`,
    });
  } else {
    console.warn(
      `Webhook de MP: pago de $${pago.transaction_amount} en placa ${device.id} sin combo que coincida — no se acredita nada.`
    );
  }

  await admin.from("mp_payments").insert({
    device_id: device.id,
    mp_payment_id: pago.id,
    amount: pago.transaction_amount,
    fichas_dispensed: fichas,
    status: fichas > 0 ? "acreditado" : "sin_match",
  });

  // En modo fijo, re-bloqueamos el precio para la próxima venta (el QR
  // instore de MP libera el monto fijo después de cada pago).
  if (device.mode === "fijo" && device.mp_monto_fijo && device.pos_id) {
    try {
      await bloquearPrecioFijo(
        profile.mp_access_token,
        profile.mp_user_id!,
        device.pos_id,
        device.caja_name ?? "QRcade",
        Number(device.mp_monto_fijo)
      );
    } catch (err) {
      console.error("No se pudo re-bloquear el precio fijo tras el pago:", err);
    }
  }

  return NextResponse.json({ ok: true, fichas_dispensed: fichas });
}
