import "server-only";
import type { SupabaseClient } from "@supabase/supabase-js";
import type { DeviceEventType } from "@/lib/supabase/types";

/**
 * Anota un evento en la línea de tiempo de la placa (device_events), para
 * que el dueño lo vea en el panel sin tener que leer el log serial. Nunca
 * tira si falla — es puramente informativo, no debe romper el flujo
 * principal (check-in, dispense, etc.) si el insert no anda.
 */
export async function logDeviceEvent(
  admin: SupabaseClient,
  deviceId: string,
  type: DeviceEventType,
  message: string
): Promise<void> {
  try {
    await admin.from("device_events").insert({ device_id: deviceId, type, message });
  } catch {
    // Silencioso a propósito: un evento de log que falla no puede tirar
    // abajo un check-in o un dispense real.
  }
}
