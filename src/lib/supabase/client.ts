"use client";

import { createBrowserClient } from "@supabase/ssr";

/**
 * Cliente de Supabase para usar en Client Components. Usa la anon key
 * (segura para exponer al browser) — todo el acceso a datos pasa por RLS.
 */
export function createClient() {
  return createBrowserClient(
    process.env.NEXT_PUBLIC_SUPABASE_URL!,
    process.env.NEXT_PUBLIC_SUPABASE_ANON_KEY!
  );
}
