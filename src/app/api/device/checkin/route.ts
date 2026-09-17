import { NextResponse } from "next/server";
import { createHash } from "crypto";
import { createAdminClient } from "@/lib/supabase/admin";
import { generateClaimCode } from "@/lib/device-codes";
import { logDeviceEvent } from "@/lib/device-events";

// Si pasó más que esto desde el check-in anterior, lo tratamos como que la
// placa se había desconectado y volvió — el check-in normal es cada 5s
// (CHECKIN_INTERVAL_MS en el firmware), así que un hueco de 20s+ ya es un
// reinicio o corte de WiFi real, no jitter de red.
const RECONNECT_GAP_MS = 20000;

/**
 * Check-in periódico de la ESP32. No requiere sesión de usuario: se
 * autentica con la "clave de fábrica" embebida en el firmware que
 * compila el admin. Body esperado:
 *
 *   { mac, factory_key, firmware_version?, wifi_rssi? }
 *
 * - Si la MAC no existe todavía: la crea como "unclaimed" (sólo si la
 *   factory_key es válida) y le devuelve un claim_code para mostrar en
 *   pantalla.
 * - Si ya existe: actualiza last_seen_at/firmware/rssi y devuelve su
 *   estado actual (claimed/revoked/unclaimed) más si hay firmware nuevo
 *   disponible para que la ESP se lo pueda avisar al dueño... aunque en
 *   este diseño el aviso vive en la app, no en la ESP.
 */
export async function POST(request: Request) {
  const body = await request.json().catch(() => null);

  if (!body?.mac || !body?.factory_key) {
    return NextResponse.json(
      { error: "Faltan campos: mac y factory_key son obligatorios." },
      { status: 400 }
    );
  }

  const mac = String(body.mac).toUpperCase();
  const admin = createAdminClient();

  const { data: setting } = await admin
    .from("app_settings")
    .select("value")
    .eq("key", "factory_key_hash")
    .single();

  const providedHash = createHash("sha256")
    .update(String(body.factory_key))
    .digest("hex");

  const isValidKey = setting?.value && setting.value === providedHash;

  const { data: existing } = await admin
    .from("devices")
    .select("*")
    .eq("mac", mac)
    .maybeSingle();

  if (!existing && !isValidKey) {
    await admin.from("device_registration_attempts").insert({
      mac,
      accepted: false,
      reason: "factory_key inválida",
    });
    return NextResponse.json(
      { error: "No autorizada." },
      { status: 403 }
    );
  }

  const nowFields = {
    last_seen_at: new Date().toISOString(),
    firmware_version: body.firmware_version ?? null,
    wifi_rssi: typeof body.wifi_rssi === "number" ? body.wifi_rssi : null,
  };

  let device = existing;

  if (!device) {
    const claimCode = generateClaimCode();
    const { data: created, error } = await admin
      .from("devices")
      .insert({ mac, claim_code: claimCode, status: "unclaimed", ...nowFields })
      .select("*")
      .single();

    if (error) {
      return NextResponse.json({ error: error.message }, { status: 500 });
    }

    await admin.from("device_registration_attempts").insert({
      mac,
      accepted: true,
      reason: "alta nueva",
    });

    device = created;
  } else {
    const gapMs = existing.last_seen_at
      ? Date.now() - new Date(existing.last_seen_at).getTime()
      : null;

    const { data: updated, error } = await admin
      .from("devices")
      .update(nowFields)
      .eq("id", existing.id)
      .select("*")
      .single();

    if (error) {
      return NextResponse.json({ error: error.message }, { status: 500 });
    }
    device = updated;

    if (gapMs !== null && gapMs >= RECONNECT_GAP_MS && device.status === "claimed") {
      const segundos = Math.round(gapMs / 1000);
      const rssi = nowFields.wifi_rssi !== null ? ` (${nowFields.wifi_rssi} dBm)` : "";
      await logDeviceEvent(
        admin,
        device.id,
        "wifi_reconnect",
        `Reconectada al WiFi tras ${segundos}s sin check-in${rssi}.`
      );
    }
  }

  if (device.status === "revoked") {
    return NextResponse.json({ status: "revoked" });
  }

  if (device.status === "unclaimed") {
    return NextResponse.json({
      status: "unclaimed",
      claim_code: device.claim_code,
    });
  }

  // claimed: le mandamos los comandos pendientes (restart / test_dispense /
  // a futuro dispense con cantidad) y lo necesario para armar el QR de
  // cobro. La ESP los ejecuta y los da por hechos apenas los recibe (no hay
  // ack de vuelta) — por eso los marcamos como reconocidos ya en esta misma
  // respuesta.
  const { data: pendingCommands } = await admin
    .from("device_commands")
    .select("id, command")
    .eq("device_id", device.id)
    .is("acknowledged_at", null)
    .order("created_at", { ascending: true });

  if (pendingCommands && pendingCommands.length > 0) {
    await admin
      .from("device_commands")
      .update({ acknowledged_at: new Date().toISOString() })
      .in(
        "id",
        pendingCommands.map((c) => c.id)
      );
  }

  return NextResponse.json({
    status: "claimed",
    is_paused: device.is_paused,
    // Payload del QR dinámico de MP, cacheado la última vez que el dueño
    // guardó su configuración de cobro (ver saveDeviceBillingAction). Si
    // todavía no configuró nada, viene null y la ESP muestra "cobro sin
    // configurar".
    qr_data: device.qr_data,
    commands: (pendingCommands ?? []).map((c) => c.command),
  });
}
