-- ─────────────────────────────────────────────────────────────
-- Logos de la pantalla de la cafetera, subidos desde la app en vez de
-- copiarlos a mano a la microSD.
--
--   - LOGO1.BIN ("logo del cliente"): uno por máquina, lo sube el
--     DUEÑO desde /dashboard/maquinas/[id] (sólo cafeteras). Se guarda
--     en devices.display_logo_cliente_url/version.
--   - LOGO2.BIN ("logo NexoQR"): uno solo, GLOBAL, lo sube un ADMIN
--     desde /admin/firmware y lo usan TODAS las placas sin importar el
--     dueño. Se guarda en app_settings (claves logo_nexo_url /
--     logo_nexo_version) — no hace falta columna nueva en devices.
--
-- *_version es un epoch en milisegundos (Date.now() del browser al
-- subir) — la pantalla sólo lo usa para detectar "cambió" comparando
-- contra el último que ya descargó, nunca lo interpreta como fecha.
-- No toca nada de lo que ya cobra ni de device_events/checkin.
-- ─────────────────────────────────────────────────────────────

alter table public.devices
  add column if not exists display_logo_cliente_url text,
  add column if not exists display_logo_cliente_version bigint;

comment on column public.devices.display_logo_cliente_url is 'URL pública (Supabase Storage) del LOGO1.BIN ("logo del cliente") de esta máquina, subido por el dueño desde /dashboard. Null = todavía no subió ninguno por la app (la pantalla sigue usando lo que ya tenga en la microSD, no se toca).';
comment on column public.devices.display_logo_cliente_version is 'Epoch ms de la última subida del logo del cliente — la pantalla lo compara contra el último que ya descargó para saber si hay que volver a bajarlo.';
