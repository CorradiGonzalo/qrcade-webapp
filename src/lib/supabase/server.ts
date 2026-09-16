import { createServerClient } from "@supabase/ssr";
import { cookies } from "next/headers";

/**
 * Cliente de Supabase para Server Components / Route Handlers / Server
 * Actions. Lee y escribe la sesión desde las cookies del request. Sigue
 * respetando RLS (usa la anon key) — para operaciones que necesitan saltar
 * RLS (crear usuarios, aprobar firmware, etc.) usar admin.ts en su lugar.
 */
export async function createClient() {
  const cookieStore = await cookies();

  return createServerClient(
    process.env.NEXT_PUBLIC_SUPABASE_URL!,
    process.env.NEXT_PUBLIC_SUPABASE_ANON_KEY!,
    {
      cookies: {
        getAll() {
          return cookieStore.getAll();
        },
        setAll(cookiesToSet) {
          try {
            cookiesToSet.forEach(({ name, value, options }) =>
              cookieStore.set(name, value, options)
            );
          } catch {
            // Se llama desde un Server Component sin permiso de escritura;
            // el middleware ya se encarga de refrescar la sesión.
          }
        },
      },
    }
  );
}
