# QRcade — Panel de administración

Panel web para gestionar la flota de máquinas QRcade (ESP32). Next.js 16
(App Router) + Supabase (Postgres, Auth, RLS, Storage).

## Qué incluye este primer corte

- **Login por usuario/clave** (no por mail) + cambio de contraseña forzado
  y captura de mail de recuperación en el primer ingreso.
- **Alta de placas sin trabajo manual**: la ESP se autoriza sola en su
  primer boot con una clave de fábrica embebida en el firmware; el dueño
  la vincula a su cuenta con el código de 6 caracteres que la placa
  muestra en pantalla.
- **Panel admin** (`/admin/equipos`, `/admin/usuarios`, `/admin/firmware`):
  auditoría de placas (reclamada / sin reclamar / revocada, con
  revocar/reactivar), alta de usuarios con clave temporal, y publicación
  de nuevas versiones de firmware.
- **Panel del dueño** (`/dashboard`): sus máquinas, pausar/reiniciar,
  vincular una máquina nueva por código, aviso opt-in de firmware nuevo, y
  configuración de cobro (Access Token de MP, caja, monto fijo) — todo lo
  carga el dueño, no el admin.
- **API para la ESP**: `POST /api/device/checkin` (check-in periódico con
  la clave de fábrica).

## Firmware ESP32 (`firmware/qrcade_v1/`)

Firmware v1.0: la placa ya no le habla a Mercado Pago directamente. Hace
check-in cada 5s a `POST /api/device/checkin` (clave de fábrica +
MAC), y ese endpoint le devuelve su estado (`unclaimed` / `claimed` /
`revoked`), el QR de cobro cuando el backend lo tenga armado, y los
comandos pendientes (`restart`, `test_dispense`) que dispara en el
momento. Esto saca de la ESP toda llamada HTTPS a la API de MP, que era
la causa del bug de cuelgue del firmware anterior.

Antes de compilar, completar en `qrcade_v1.ino`:

- `FACTORY_KEY`: la clave de fábrica generada en el paso 3 de abajo
  (la misma para toda la tanda de placas).
- `BACKEND_HOST`: el dominio donde quede publicado el panel (sin
  `https://` ni barra final).

El portal cautivo (`QRcade_Config`) ahora sólo pide WiFi — Access
Token, Local, Caja y Monto se cargan desde el panel del dueño, no
desde la placa. El código de 6 caracteres para vincular la placa no
se muestra en pantalla: lo ve el admin en `/admin/equipos` y se lo
pasa al dueño.

`qr_data` ahora sí viene poblado desde el backend en cuanto el dueño
guarda la configuración de cobro de la placa (ver "Integración con
Mercado Pago" abajo) — la ESP lo recibe en el check-in y lo dibuja tal
cual, sin llamar a MP en ningún momento.

Librerías necesarias (Arduino Library Manager): WiFiManager (tzapu),
ArduinoJson, GFX Library for Arduino (moononournation). `qrcode_impl.c/h`
(ricmoo/Nayuki, MIT) ya están incluidas en la carpeta del firmware.

## Integración con Mercado Pago

Cada dueño carga UN SOLO Access Token para toda su cuenta, desde
`/dashboard/cuenta` (se valida contra `GET /users/me` de MP al
guardarlo, y se cachea también el `mp_user_id` — hace falta para
poder identificar de quién es un pago cuando llega el webhook). Después,
en cada máquina (`/dashboard/maquinas/[id]/cobro`), el dueño elige:

- **Local y caja**: si el local ya existe en su cuenta de MP se
  reutiliza, si no se crea; la caja siempre es nueva por placa. Todo
  esto lo arma el backend contra la API de MP al guardar
  (`saveDeviceBillingAction` → `src/lib/mercadopago.ts`), nunca la ESP.
- **Modo "monto fijo"**: el QR queda bloqueado a un precio — 1 pago = 1
  ficha.
- **Modo "fichas por monto abierto"**: el QR acepta cualquier monto: el
  dueño carga una lista de combos `{monto, fichas}` en la misma
  pantalla, y sólo se acredita si el pago coincide EXACTO con un monto
  de la lista (sin match, no se acredita nada — eso lo arregla cada
  dueño con su cliente, no es un caso que QRcade tenga que resolver).

Los pagos se confirman vía webhook (`POST /api/mp/webhook`) — cada
dueño tiene que pegar esa URL en su propia cuenta de MP (Tu negocio →
Webhooks → Notificaciones de pago; la URL exacta a copiar aparece en
`/dashboard/cuenta`). MP manda el `user_id` de la cuenta junto con cada
notificación, así que el webhook sabe con qué token consultar el pago
sin necesitar sesión. Cuando el pago aprueba y matchea (monto fijo, o
combo exacto), se encola un comando `dispense:N` en `device_commands` —
la ESP ya sabe interpretarlo desde el firmware v1.0. Todo pago
procesado (matcheado o no) queda auditado en `mp_payments`, con dedupe
por `mp_payment_id` porque MP puede reenviar la misma notificación más
de una vez.

## Lo que falta para producción (a propósito, fuera de este corte)

- **Cifrado del Access Token de MP en reposo**: hoy se guarda como texto
  plano en `profiles.mp_access_token`. Para producción conviene cifrarlo
  (pgsodium/Vault de Supabase, o cifrado a nivel aplicación) antes de que
  haya tokens reales cargados.
- **Actualización de firmware**: la ESP nunca descarga nada sola — sólo
  se entera en el check-in de que hay una versión nueva, se lo avisa al
  dueño en el panel (`FirmwareBanner`), y el dueño acepta o rechaza
  (`device_firmware_status`). Lo que falta es lo que pasa después de
  aceptar: hoy no hay ningún mecanismo para que el binario realmente
  llegue a la placa (ni push automático ni instrucción de descarga) —
  se define a propósito más adelante, junto con cómo se compila/firma
  cada `.bin` que se sube desde `/admin/firmware`.
- Recuperación de contraseña self-service (hoy el admin la resetea a
  mano desde `/admin/usuarios` — la pantalla para eso todavía no está,
  pero el usuario y el flag `must_change_password` ya soportan ese caso:
  bastaría un botón "Resetear clave" ahí).

## Puesta en marcha

### 1. Crear el proyecto de Supabase

En [supabase.com](https://supabase.com) creá un proyecto nuevo. Andá a
**Project Settings → API** y copiá `Project URL`, `anon public key` y
`service_role key`.

### 2. Correr las migraciones

En el **SQL Editor** de Supabase, corré en orden:

1. `supabase/migrations/0001_init.sql`
2. `supabase/migrations/0002_seed_settings.sql` (antes reemplazá el valor
   de ejemplo por el hash real — ver los comentarios del archivo)
3. `supabase/migrations/0003_test_dispense_command.sql`
4. `supabase/migrations/0004_mp_fichas.sql`

### 3. Generar la clave de fábrica

```bash
openssl rand -hex 24                                  # esto va en el firmware
echo -n "LA_CLAVE_DE_ARRIBA" | openssl dgst -sha256    # esto va en app_settings.factory_key_hash
```

Guardá la clave (no el hash) vos — es la que se embebe en el `.ino` que
compilás para cada placa.

### 4. Crear el bucket de firmware (Storage)

En **Storage**, creá un bucket público llamado `firmware`. Ahí se suben
los `.bin` desde `/admin/firmware`.

### 5. Variables de entorno

```bash
cp .env.local.example .env.local
```

Completá `NEXT_PUBLIC_SUPABASE_URL`, `NEXT_PUBLIC_SUPABASE_ANON_KEY` y
`SUPABASE_SERVICE_ROLE_KEY` con los datos del paso 1.

### 6. Crear el primer admin

Los usuarios normales se crean desde `/admin/usuarios`, pero el primer
admin no tiene quién lo cree — se hace una vez a mano. En el **SQL
Editor** de Supabase:

```sql
-- 1) Creá el usuario de auth (reemplazá el mail/clave)
select auth.uid() from auth.users; -- sólo para confirmar que estás conectado

-- Mejor: usá el dashboard de Supabase → Authentication → Add user
-- (creá uno con email admin@qrcade.internal y una contraseña)
-- y después completá su perfil:

insert into public.profiles (id, username, role, must_change_password, display_name)
values (
  'EL_UUID_DEL_USUARIO_QUE_CREASTE',
  'gonzalo',
  'admin',
  false,
  'Gonzalo Corradi'
);
```

### 7. Correr localmente

```bash
npm install
npm run dev
```

Abrí `http://localhost:3000` → te manda a `/login`.

## Estructura

```
src/
  app/
    login/                    página de login
    primer-ingreso/            modal de cambio de clave + mail de recuperación
    admin/                     panel del admin (requiere role=admin)
    dashboard/                 panel del dueño de máquinas
    api/device/checkin         check-in de la ESP (clave de fábrica)
    api/device/claim           vincular placa por código (alternativa HTTP a la server action)
  components/                  UI compartida
  lib/
    supabase/                  clientes (browser/server/admin) + tipos
    actions/                   server actions (auth, admin, devices)
    device-codes.ts            generador de claim codes y contraseñas temporales
supabase/migrations/           esquema SQL + RLS
```
