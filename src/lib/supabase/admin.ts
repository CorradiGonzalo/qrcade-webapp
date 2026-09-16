import "server-only";
import { createClient as createSupabaseClient } from "@supabase/supabase-js";

/**
 * Cliente con la service role key: salta RLS por completo. SOLO se importa
 * desde código que corre en el servidor (API routes / Server Actions) y
 * SIEMPRE después de validar vos mismo el permiso (sesión + rol) — este
 * cliente no aplica ninguna regla de seguridad por su cuenta.
 *
 * El `import "server-only"` de arriba hace fallar el build si algún
 * componente de cliente llega a importar este archivo por error.
 */
export function createAdminClient() {
  return createSupabaseClient(
    process.env.NEXT_PUBLIC_SUPABASE_URL!,
    process.env.SUPABASE_SERVICE_ROLE_KEY!,
    {
      auth: {
        autoRefreshToken: false,
        persistSession: false,
      },
    }
  );
}
