import Link from "next/link";
import { ClaimDeviceForm } from "@/components/dashboard/ClaimDeviceForm";

export default function AgregarMaquinaPage() {
  return (
    <div className="flex min-h-[80vh] items-center justify-center">
      <div className="w-[480px]">
        <Link
          href="/dashboard"
          className="mb-4 inline-block text-xs text-ink-muted hover:text-ink"
        >
          ← Volver a Mis máquinas
        </Link>
        <h1 className="mb-2 text-center text-2xl font-bold tracking-tight">
          Agregar máquina
        </h1>
        <p className="mb-8 text-center text-[13px] leading-relaxed text-ink-muted">
          Encendé la peluchera y conectala a tu WiFi desde la red{" "}
          <b className="text-ink">QRcade_Config</b>. Cuando termine, su
          pantalla va a mostrar un código — escribilo acá para vincularla a
          tu cuenta.
        </p>

        <ClaimDeviceForm />

        <div className="mt-4 rounded-lg border border-info-line bg-info-bg px-4 py-3.5 text-center text-[11px] text-info">
          Solo las máquinas que instaló QRcade generan este código — si el
          tuyo no aparece, contactá a soporte.
        </div>
      </div>
    </div>
  );
}
