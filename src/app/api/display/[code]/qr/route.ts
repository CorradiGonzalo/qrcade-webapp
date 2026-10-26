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
    .select("qr_data, is_paused, mode, mp_monto_fijo")
    .eq("display_pairing_code", code)
    .single<Pick<Device, "qr_data" | "is_paused" | "mode" | "mp_monto_fijo">>();

  if (!device) {
    return NextResponse.json({ error: "Código no encontrado." }, { status: 404 });
  }

  return NextResponse.json({
    qr_data: device.qr_data,
    is_paused: device.is_paused,
    mode: device.mode,
    mp_monto_fijo: device.mp_monto_fijo,
  });
}
