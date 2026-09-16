import { NextResponse } from "next/server";
import { createClient } from "@/lib/supabase/server";

/**
 * El dueño acepta o posterga una actualización de firmware notificada
 * para una de sus máquinas. Pasa por RLS normal (no hace falta admin
 * client): la policy de device_firmware_status ya exige que el device
 * le pertenezca.
 */
export async function POST(request: Request) {
  const supabase = await createClient();
  const {
    data: { user },
  } = await supabase.auth.getUser();

  if (!user) {
    return NextResponse.json({ error: "No autenticado." }, { status: 401 });
  }

  const body = await request.json().catch(() => null);
  const { device_id, firmware_version_id, status } = body ?? {};

  if (
    !device_id ||
    !firmware_version_id ||
    !["accepted", "dismissed"].includes(status)
  ) {
    return NextResponse.json({ error: "Body inválido." }, { status: 400 });
  }

  const { error } = await supabase
    .from("device_firmware_status")
    .update({ status, updated_at: new Date().toISOString() })
    .eq("device_id", device_id)
    .eq("firmware_version_id", firmware_version_id);

  if (error) {
    return NextResponse.json({ error: error.message }, { status: 500 });
  }

  return NextResponse.json({ ok: true });
}
