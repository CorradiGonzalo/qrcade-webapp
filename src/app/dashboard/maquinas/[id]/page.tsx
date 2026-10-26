import Link from "next/link";
import { notFound } from "next/navigation";
import QRCode from "qrcode";
import { requireActiveUser } from "@/lib/supabase/auth";
import { CafeteraConfigForm } from "@/components/dashboard/CafeteraConfigForm";
import { DeviceActions } from "@/components/dashboard/DeviceActions";
import { DisplayPairingCard } from "@/components/dashboard/DisplayPairingCard";
import { DeviceNameEditor } from "@/components/dashboard/DeviceNameEditor";
import { FirmwareBanner } from "@/components/dashboard/FirmwareBanner";
import { SalesPeriodFilter } from "@/components/dashboard/SalesPeriodFilter";
import { resolverRangoFechas } from "@/lib/sales-period";
import type {
  Device,
  DeviceEvent,
  FirmwareVersion,
  Profile,
} from "@/lib/supabase/types";

/** Tope de filas que se traen de mp_payments por período — de sobra para
 * el volumen de una máquina individual; evita una query sin fin si
 * alguien elige "Todo" en una placa con años de historial. */
const MAX_PAGOS = 500;
/** Cuántas filas se listan en el detalle (los totales sí usan MAX_PAGOS). */
const MAX_PAGOS_LISTADOS = 50;

const EVENT_LABELS: Record<DeviceEvent["type"], string> = {
  wifi_reconnect: "📶 WiFi",
  dispense_test: "🧪 Prueba",
  dispense_payment: "💸 Pago",
  restart_requested: "⟲ Reinicio",
  firmware_update: "🆕 Firmware",
  clean_cycle: "🧽 Limpieza",
};

export default async function DeviceDetailPage(
  props: PageProps<"/dashboard/maquinas/[id]">
) {
  const { id } = await props.params;
  const sp = await props.searchParams;
  const { user, supabase } = await requireActiveUser();

  const { data: device } = await supabase
    .from("devices")
    .select("*")
    .eq("id", id)
    .eq("owner_id", user.id)
    .single<Device>();

  if (!device) notFound();

  const { data: pendingStatus } = await supabase
    .from("device_firmware_status")
    .select("firmware_version_id")
    .eq("device_id", device.id)
    .eq("status", "notified")
    .maybeSingle();

  let pendingFirmware: FirmwareVersion | null = null;
  if (pendingStatus) {
    const { data: firmware } = await supabase
      .from("firmware_versions")
      .select("*")
      .eq("id", pendingStatus.firmware_version_id)
      .single<FirmwareVersion>();
    pendingFirmware = firmware;
  }

  const hasQrConfig = Boolean(device.qr_data);
  const esCafetera = device.kind === "cafetera";

  let hasAccountToken = false;
  if (esCafetera) {
    const { data: profile } = await supabase
      .from("profiles")
      .select("mp_access_token")
      .eq("id", user.id)
      .single<Pick<Profile, "mp_access_token">>();
    hasAccountToken = Boolean(profile?.mp_access_token);
  }

  const periodoParam = typeof sp.periodo === "string" ? sp.periodo : undefined;
  const desdeParam = typeof sp.desde === "string" ? sp.desde : undefined;
  const hastaParam = typeof sp.hasta === "string" ? sp.hasta : undefined;
  const rango = resolverRangoFechas({
    periodo: periodoParam,
    desde: desdeParam,
    hasta: hastaParam,
  });

  let pagosQuery = supabase
    .from("mp_payments")
    .select("amount, fichas_dispensed, status, created_at")
    .eq("device_id", device.id)
    .order("created_at", { ascending: false })
    .limit(MAX_PAGOS);

  if (rango.desde) pagosQuery = pagosQuery.gte("created_at", rango.desde.toISOString());
  if (rango.hasta) pagosQuery = pagosQuery.lt("created_at", rango.hasta.toISOString());

  const { data: pagos } = await pagosQuery;

  const { data: eventos } = await supabase
    .from("device_events")
    .select("type, message, created_at")
    .eq("device_id", device.id)
    .order("created_at", { ascending: false })
    .limit(15);

  const acreditados = (pagos ?? []).filter((p) => (p.fichas_dispensed ?? 0) > 0);
  const totalFichas = acreditados.reduce((acc, p) => acc + (p.fichas_dispensed ?? 0), 0);
  const totalCobrado = acreditados.reduce((acc, p) => acc + Number(p.amount ?? 0), 0);
  const totalVentas = acreditados.length;
  const pagosTope = (pagos ?? []).length >= MAX_PAGOS;

  const qrImage = device.qr_data
    ? await QRCode.toDataURL(device.qr_data, { margin: 1, width: 180 })
    : null;

  return (
    <div>
      <div className="mb-7 flex flex-col gap-3 sm:flex-row sm:items-start sm:justify-between">
        <div>
          <Link
            href="/dashboard"
            className="mb-1.5 block text-xs text-ink-faint hover:text-ink"
          >
            ← Mis máquinas
          </Link>
          <DeviceNameEditor deviceId={device.id} alias={device.alias} mac={device.mac} />
          <div className="mt-1.5 font-mono text-xs text-ink-faint">
            {device.mac}
          </div>
        </div>
        <div className="flex flex-wrap items-center gap-2.5">
          <Link
            href={`/dashboard/maquinas/${device.id}/cobro`}
            className="rounded-[9px] border border-line-soft bg-surface-2 px-4 py-2.5 text-[13px] font-semibold text-ink"
          >
            ⚙ Configurar cobro
          </Link>
          <DeviceActions device={device} />
        </div>
      </div>

      {pendingFirmware && (
        <FirmwareBanner
          deviceId={device.id}
          deviceLabel={device.alias ?? device.mac}
          version={pendingFirmware.version}
          changelog={pendingFirmware.changelog}
          firmwareVersionId={pendingFirmware.id}
        />
      )}

      <div className="grid grid-cols-1 gap-6 lg:grid-cols-[1.3fr_1fr]">
        <div>
          <div className="mb-5 grid grid-cols-3 gap-2 sm:gap-3.5">
            <div className="rounded-xl border border-line bg-surface p-4">
              <div className="mb-2 text-[11px] font-bold text-ink-faint">
                ESTADO
              </div>
              <div
                className={`text-[15px] font-bold ${
                  device.is_paused ? "text-warn" : "text-ok"
                }`}
              >
                ● {device.is_paused ? "Pausada" : "Activa"}
              </div>
            </div>
            <div className="rounded-xl border border-line bg-surface p-4">
              <div className="mb-2 text-[11px] font-bold text-ink-faint">
                FIRMWARE
              </div>
              <div className="text-[15px] font-bold">
                {device.firmware_version ?? "—"}
              </div>
            </div>
            <div className="rounded-xl border border-line bg-surface p-4">
              <div className="mb-2 text-[11px] font-bold text-ink-faint">
                SEÑAL WIFI
              </div>
              <div className="text-[15px] font-bold">
                {device.wifi_rssi ? `${device.wifi_rssi} dBm` : "—"}
              </div>
            </div>
          </div>

          <div className="rounded-2xl border border-line bg-surface p-[22px]">
            <div className="mb-3 flex flex-wrap items-center justify-between gap-2">
              <div className="text-sm font-bold">Ventas</div>
              <div className="text-[11px] text-ink-faint">{rango.label}</div>
            </div>

            <SalesPeriodFilter periodoActivo={rango.periodo} />

            {acreditados.length === 0 ? (
              <p className="text-xs text-ink-muted">
                No hay pagos acreditados en este período. En cuanto Mercado
                Pago confirme un cobro (vía webhook) va a aparecer acá.
              </p>
            ) : (
              <>
                <div className="mb-4 flex flex-wrap gap-6">
                  <div>
                    <div className="text-[11px] text-ink-faint">Recaudado</div>
                    <div className="text-lg font-bold">${totalCobrado}</div>
                  </div>
                  <div>
                    <div className="text-[11px] text-ink-faint">
                      Fichas entregadas
                    </div>
                    <div className="text-lg font-bold">{totalFichas}</div>
                  </div>
                  <div>
                    <div className="text-[11px] text-ink-faint">Ventas</div>
                    <div className="text-lg font-bold">{totalVentas}</div>
                  </div>
                </div>
                <div className="space-y-1.5">
                  {(pagos ?? []).slice(0, MAX_PAGOS_LISTADOS).map((p, i) => (
                    <div
                      key={i}
                      className="flex flex-col gap-0.5 text-xs text-ink-muted sm:flex-row sm:justify-between sm:gap-3"
                    >
                      <span>
                        {new Date(p.created_at).toLocaleString("es-AR")}
                      </span>
                      <span
                        className={
                          (p.fichas_dispensed ?? 0) > 0 ? "text-ok" : "text-danger"
                        }
                      >
                        ${p.amount} →{" "}
                        {(p.fichas_dispensed ?? 0) > 0
                          ? `${p.fichas_dispensed} ficha(s)`
                          : "sin match"}
                      </span>
                    </div>
                  ))}
                </div>
                {(pagos ?? []).length > MAX_PAGOS_LISTADOS && (
                  <p className="mt-2.5 text-[11px] text-ink-faint">
                    Mostrando los últimos {MAX_PAGOS_LISTADOS} de{" "}
                    {(pagos ?? []).length}
                    {pagosTope ? "+" : ""} pagos de este período — los
                    totales de arriba sí los incluyen a todos
                    {pagosTope ? ` (hasta ${MAX_PAGOS})` : ""}.
                  </p>
                )}
              </>
            )}
          </div>

          <div className="mt-4 rounded-2xl border border-line bg-surface p-[22px]">
            <div className="mb-3 text-sm font-bold">Actividad</div>
            {!eventos || eventos.length === 0 ? (
              <p className="text-xs text-ink-muted">
                Todavía no hay eventos registrados — van a aparecer acá
                reconexiones de WiFi, disparos del relé (prueba o pago) y
                reinicios pedidos desde el panel.
              </p>
            ) : (
              <div className="space-y-1.5">
                {eventos.map((e, i) => (
                  <div
                    key={i}
                    className="flex flex-col gap-0.5 text-xs text-ink-muted sm:flex-row sm:items-start sm:justify-between sm:gap-3"
                  >
                    <span className="shrink-0 text-ink-faint">
                      {new Date(e.created_at).toLocaleString("es-AR")}
                    </span>
                    <span className="text-ink sm:text-right">
                      {EVENT_LABELS[e.type as DeviceEvent["type"]] ?? e.type}{" "}
                      — {e.message}
                    </span>
                  </div>
                ))}
              </div>
            )}
          </div>
        </div>

        <div>
          <div className="rounded-2xl border border-line bg-surface p-6 text-center">
            <div className="mb-4 text-left text-sm font-bold">
              Código QR activo
            </div>
            {hasQrConfig ? (
              <>
                <div className="inline-block rounded-lg bg-white p-3">
                  {qrImage ? (
                    // eslint-disable-next-line @next/next/no-img-element
                    <img
                      src={qrImage}
                      alt="QR de cobro"
                      width={180}
                      height={180}
                    />
                  ) : (
                    <div className="flex h-[180px] w-[180px] items-center justify-center text-xs text-ink-faint">
                      Error generando QR
                    </div>
                  )}
                </div>
                <div className="mt-3.5 text-[11px] text-ink-faint">
                  {esCafetera
                    ? "Esta máquina no tiene pantalla — descargá el PDF e imprimilo para pegarlo en la cafetera."
                    : "Esto es una vista previa — el que importa es el que dibuja la placa."}
                </div>
                <div className="mt-1 text-[11px] text-ink-faint">
                  {device.local_name} · {device.caja_name} ·{" "}
                  {device.mode === "fijo"
                    ? `$${device.mp_monto_fijo} fijo`
                    : "monto abierto (combos)"}
                </div>
                {esCafetera && (
                  <a
                    href={`/api/device/${device.id}/qr-pdf`}
                    className="mt-4 inline-flex items-center justify-center rounded-lg bg-brand px-4 py-2.5 text-[13px] font-semibold text-white hover:opacity-90"
                  >
                    ⬇ Descargar QR en PDF
                  </a>
                )}
              </>
            ) : (
              <div className="rounded-lg border border-dashed border-line-soft p-8 text-xs text-ink-faint">
                Todavía no configuraste el cobro de esta máquina.
                <Link
                  href={`/dashboard/maquinas/${device.id}/cobro`}
                  className="mt-2 block font-semibold text-brand"
                >
                  Configurar ahora →
                </Link>
              </div>
            )}
          </div>

          {esCafetera && (
            <div className="mt-4">
              <CafeteraConfigForm device={device} hasAccountToken={hasAccountToken} />
            </div>
          )}

          {esCafetera && <DisplayPairingCard device={device} />}

          <div className="mt-4 rounded-2xl border border-line bg-surface p-5">
            <div className="mb-3 text-sm font-bold">Info del equipo</div>
            <div className="mb-2 flex justify-between text-xs text-ink-muted">
              <span>Firmware</span>
              <span className="text-ink">{device.firmware_version ?? "—"}</span>
            </div>
            <div className="mb-2 flex justify-between text-xs text-ink-muted">
              <span>Última conexión</span>
              <span className="text-ink">
                {device.last_seen_at
                  ? new Date(device.last_seen_at).toLocaleString("es-AR")
                  : "—"}
              </span>
            </div>
            <div className="flex justify-between text-xs text-ink-muted">
              <span>Vinculada</span>
              <span className="text-ink">
                {device.claimed_at
                  ? new Date(device.claimed_at).toLocaleDateString("es-AR")
                  : "—"}
              </span>
            </div>
          </div>
        </div>
      </div>
    </div>
  );
}
