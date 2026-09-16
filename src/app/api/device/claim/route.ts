import { NextResponse } from "next/server";
import { createClient } from "@/lib/supabase/server";
import { createAdminClient } from "@/lib/supabase/admin";

/**
 * El dueño vincula una ESP a su cuenta ingresando el código que ve en la
 * pantalla de la placa. Requiere sesión (cualquier rol). El match por
 * claim_code se hace con el cliente admin porque una placa "unclaimed"
 * (owner_id null) no es visible por RLS para un usuario común — así
 * evitamos exponer el listado de placas sin reclamar.
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
  const code = String(body?.claim_code || "")
    .trim()
    .toUpperCase();
  const alias = body?.alias ? String(body.alias).trim() : null;

  if (!code) {
    return NextResponse.json({ error: "Ingresá el código." }, { status: 400 });
  }

  const admin = createAdminClient();

  const { data: device, error: findError } = await admin
    .from("devices")
    .select("id, status")
    .eq("claim_code", code)
    .eq("status", "unclaimed")
    .maybeSingle();

  if (findError) {
    return NextResponse.json({ error: findError.message }, { status: 500 });
  }

  if (!device) {
    return NextResponse.json(
      { error: "Código inválido o la máquina ya fue vinculada." },
      { status: 404 }
    );
  }

  const { error: updateError } = await admin
    .from("devices")
    .update({
      owner_id: user.id,
      status: "claimed",
      claimed_at: new Date().toISOString(),
      claim_code: null,
      alias,
    })
    .eq("id", device.id);

  if (updateError) {
    return NextResponse.json({ error: updateError.message }, { status: 500 });
  }

  return NextResponse.json({ ok: true, device_id: device.id });
}
