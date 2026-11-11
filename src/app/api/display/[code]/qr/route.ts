import { NextResponse } from "next/server";
import { createAdminClient } from "@/lib/supabase/admin";
import type { Device } from "@/lib/supabase/types";

/**
 * Endpoint de SÓLO LECTURA para una ESP32 "sólo pantalla" (sin relé,
 * sin lógica de pago) que convive con una cafetera ya armada con su
 * propia placa de cobro — ver migración 0014.
 *
 * No requiere sesión de usuario ni factory_key: se identifica con el
 * `display_pairing_code` de la máquina (generado desde el panel del
 * dueño, en /dashboard/maquinas/[id]). A propósito NO acepta nada por
 * POST, no inserta `device_commands`, no toca `last_seen_at` ni ningún
 * otro campo — esta placa nunca puede escribir nada sobre la que cobra,
 * sólo leer el QR vigente. La placa que cobra (y su firmware probado)
 * no se entera siquiera de que esto existe.
 */
export async function GET(
  _request: Request,
  { params }: { params: Promise<{ code: string }> }
) {
  const { code } = await params;

  if (!code || code.length > 16) {
    return NextResponse.json({ error: "Código inválido." }, { status: 400 });
  }

  const admin = createAdminClient();

  const { data: device } = await admin
    .from("devices")
    .select(
      "qr_data, is_paused, mode, mp_monto_fijo, empleado_pin, empleado_qr_data, empleado_mp_monto_fijo, last_payment_at, is_cleaning, display_logo_cliente_url, display_logo_cliente_version"
    )
    .eq("display_pairing_code", code)
    .single<
      Pick<
        Device,
        | "qr_data"
        | "is_paused"
        | "mode"
        | "mp_monto_fijo"
        | "empleado_pin"
        | "empleado_qr_data"
        | "empleado_mp_monto_fijo"
        | "last_payment_at"
        | "is_cleaning"
        | "display_logo_cliente_url"
        | "display_logo_cliente_version"
      >
    >();

  if (!device) {
    return NextResponse.json({ error: "Código no encontrado." }, { status: 404 });
  }

  // Logo NexoQR — ver migración 0019. Es GLOBAL (no depende de esta
  // máquina ni de su dueño), vive en app_settings en vez de en
  // devices. Si todavía no lo subió ningún admin, estas dos filas
  // simplemente no existen y quedan null — la pantalla no toca nada.
  const { data: settings } = await admin
    .from("app_settings")
    .select("key, value")
    .in("key", ["logo_nexo_url", "logo_nexo_version"]);

  const logoNexoUrl =
    settings?.find((s) => s.key === "logo_nexo_url")?.value ?? null;
  const logoNexoVersionRaw =
    settings?.find((s) => s.key === "logo_nexo_version")?.value ?? null;

  return NextResponse.json({
    qr_data: device.qr_data,
    is_paused: device.is_paused,
    mode: device.mode,
    mp_monto_fijo: device.mp_monto_fijo,
    // QR de empleado — null/vacío si el dueño no lo configuró, en cuyo
    // caso la pantalla simplemente no debe ofrecer la opción de tocar
    // para destaparlo. Ver migración 0015.
    empleado_pin: device.empleado_pin,
    empleado_qr_data: device.empleado_qr_data,
    empleado_mp_monto_fijo: device.empleado_mp_monto_fijo,
    // Ver migración 0016 — la pantalla compara esto contra el valor que
    // tenía en el poll anterior para mostrar "Pago acreditado".
    last_payment_at: device.last_payment_at,
    // Ver migración 0017 — mientras la cafetera está limpiando, la
    // pantalla NO debe mostrar el QR ni aceptar pagos (nadie recibiría
    // su café hasta que termine la limpieza).
    is_cleaning: device.is_cleaning,
    // Ver migración 0019 — logos subidos desde la app. "cliente" es
    // de ESTA máquina (null si el dueño nunca subió nada por la app);
    // "nexo" es global, igual para todas. Firmware de pantalla viejo
    // (anterior a v2.5.0) simplemente ignora estos 4 campos — no rompe
    // nada, no hace falta reflashear para aplicar esta migración.
    logo_cliente_url: device.display_logo_cliente_url,
    logo_cliente_version: device.display_logo_cliente_version,
    logo_nexo_url: logoNexoUrl,
    logo_nexo_version: logoNexoVersionRaw ? Number(logoNexoVersionRaw) : null,
  });
}
