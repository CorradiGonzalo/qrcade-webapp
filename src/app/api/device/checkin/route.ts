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

// Cuánto esperamos con una versión en "downloading" antes de volver a
// mandarle la URL del .bin a la placa. Cubre el caso de que la descarga
// haya fallado (WiFi débil, corte de luz a mitad de la descarga, etc.) sin
// mandarle la misma URL en CADA check-in mientras tanto (eso sí sería un
// martilleo inútil de la red y del backend cada 5s).
const OTA_RETRY_MS = 10 * 60 * 1000; // 10 min

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

  const firmwareUpdate = await resolverFirmwareUpdate(admin, device, nowFields.firmware_version);

  return NextResponse.json({
    status: "claimed",
    is_paused: device.is_paused,
    // Payload del QR dinámico de MP, cacheado la última vez que el dueño
    // guardó su configuración de cobro (ver saveDeviceBillingAction). Si
    // todavía no configuró nada, viene null y la ESP muestra "cobro sin
    // configurar".
    qr_data: device.qr_data,
    commands: (pendingCommands ?? []).map((c) => c.command),
    // Sólo presente cuando hay una versión aceptada por el dueño para
    // instalar (o cuyo intento anterior de descarga se dio por perdido).
    // La ESP la descarga con HTTPUpdate y se reinicia sola si sale bien —
    // ver ejecutarActualizacionFirmware() en el .ino.
    ...(firmwareUpdate ? { firmware_update: firmwareUpdate } : {}),
  });
}

/**
 * Resuelve si hay una actualización de firmware para mandarle a la placa
 * en este check-in, y lleva la máquina de estados de
 * device_firmware_status ('accepted' -> 'downloading' -> 'updated').
 *
 *  - El dueño acepta una versión desde el panel -> queda en 'accepted'.
 *  - Primer check-in tras aceptar: se la mandamos y pasa a 'downloading'.
 *  - Si sigue en 'downloading' más de OTA_RETRY_MS (la descarga anterior
 *    se debe haber perdido: corte de luz, WiFi débil, etc.), se la
 *    volvemos a mandar.
 *  - Cuando la placa reporta en un check-in la MISMA versión que estaba
 *    'downloading' (o sea: ya se flasheó y reinició sola), la marcamos
 *    'updated' y lo logueamos en device_events.
 */
async function resolverFirmwareUpdate(
  admin: ReturnType<typeof createAdminClient>,
  device: { id: string },
  reportedFirmwareVersion: string | null
) {
  const { data: pendiente } = await admin
    .from("device_firmware_status")
    .select("status, updated_at, firmware_version_id, firmware_versions(version, bin_url, changelog)")
    .eq("device_id", device.id)
    .in("status", ["accepted", "downloading"])
    .order("updated_at", { ascending: false })
    .limit(1)
    .maybeSingle();

  if (!pendiente) return null;

  const fv = Array.isArray(pendiente.firmware_versions)
    ? pendiente.firmware_versions[0]
    : pendiente.firmware_versions;

  if (!fv) return null;

  // La placa ya reportó la versión que estábamos esperando -> ya se
  // actualizó sola y reinició. Cerramos el ciclo.
  if (
    pendiente.status === "downloading" &&
    reportedFirmwareVersion &&
    reportedFirmwareVersion === fv.version
  ) {
    await admin
      .from("device_firmware_status")
      .update({ status: "updated", updated_at: new Date().toISOString() })
      .eq("device_id", device.id)
      .eq("firmware_version_id", pendiente.firmware_version_id);

    await logDeviceEvent(
      admin,
      device.id,
      "firmware_update",
      `Actualizada a ${fv.version}.`
    );

    return null;
  }

  const debeMandarla =
    pendiente.status === "accepted" ||
    (pendiente.status === "downloading" &&
      Date.now() - new Date(pendiente.updated_at).getTime() > OTA_RETRY_MS);

  if (!debeMandarla) return null;

  const yaEstabaDescargando = pendiente.status === "downloading";

  await admin
    .from("device_firmware_status")
    .update({ status: "downloading", updated_at: new Date().toISOString() })
    .eq("device_id", device.id)
    .eq("firmware_version_id", pendiente.firmware_version_id);

  await logDeviceEvent(
    admin,
    device.id,
    "firmware_update",
    yaEstabaDescargando
      ? `Reintentando descarga de ${fv.version} (no se vio confirmación de la anterior).`
      : `Descargando ${fv.version}...`
  );

  return { version: fv.version, bin_url: fv.bin_url };
}
