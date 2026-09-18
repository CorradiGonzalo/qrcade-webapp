import { headers } from "next/headers";
import Link from "next/link";

/**
 * Instructivo para que CUALQUIER dueño (no sólo el admin) configure el
 * webhook de pagos en SU PROPIA cuenta de Mercado Pago. Es un paso manual
 * obligatorio desde que Mercado Pago migró la API de QR: ya no se puede
 * cargar la URL de notificaciones por código (antes se mandaba con cada
 * pedido a la API vieja) — ahora hay que pegarla una sola vez en el panel
 * de Mercado Pago, y eso lo tiene que hacer cada dueño con su propia
 * cuenta (nadie más puede hacerlo por él/ella, ni siquiera el admin del
 * sistema — Mercado Pago pide verificar la identidad del dueño de la
 * cuenta con un código al celular antes de dejar tocar esa pantalla).
 *
 * Las imágenes son esquemas (no capturas reales de Mercado Pago): cada
 * cuenta tiene un nombre de aplicación distinto, así que un esquema
 * genérico de "dónde hacer click" es más útil y no se desactualiza si MP
 * le cambia el diseño a su panel.
 */

function EsquemaListaApps() {
  return (
    <svg viewBox="0 0 480 200" className="h-auto w-full" role="img" aria-label="Panel de Tus integraciones, con varias aplicaciones listadas">
      <rect x="0" y="0" width="480" height="200" rx="12" fill="#15181f" />
      <rect x="20" y="18" width="160" height="14" rx="4" fill="#5b6472" />
      <g>
        <rect x="20" y="50" width="440" height="42" rx="8" fill="#1f242e" stroke="#3a4150" />
        <circle cx="42" cy="71" r="10" fill="#2c8f6b" />
        <rect x="64" y="62" width="140" height="10" rx="3" fill="#c6ccd6" />
        <rect x="64" y="78" width="90" height="8" rx="3" fill="#6b7280" />
        <rect x="380" y="63" width="60" height="18" rx="9" fill="#2f60d6" />
      </g>
      <g>
        <rect x="20" y="102" width="440" height="42" rx="8" fill="#1f242e" stroke="#3a4150" />
        <circle cx="42" cy="123" r="10" fill="#4b5563" />
        <rect x="64" y="114" width="120" height="10" rx="3" fill="#c6ccd6" />
        <rect x="64" y="130" width="70" height="8" rx="3" fill="#6b7280" />
      </g>
      <g>
        <rect x="20" y="154" width="440" height="30" rx="8" fill="#1a1e26" stroke="#2a2f3a" />
        <rect x="34" y="165" width="100" height="8" rx="3" fill="#4b5563" />
      </g>
    </svg>
  );
}

function EsquemaMenuWebhooks() {
  return (
    <svg viewBox="0 0 480 200" className="h-auto w-full" role="img" aria-label="Menú lateral de la aplicación con la sección Webhooks resaltada">
      <rect x="0" y="0" width="480" height="200" rx="12" fill="#15181f" />
      <rect x="20" y="18" width="120" height="164" rx="8" fill="#1a1e26" />
      <rect x="34" y="34" width="90" height="8" rx="3" fill="#4b5563" />
      <rect x="34" y="58" width="70" height="8" rx="3" fill="#4b5563" />
      <rect x="34" y="82" width="90" height="8" rx="3" fill="#4b5563" />
      <rect x="26" y="102" width="106" height="26" rx="6" fill="#2f60d6" />
      <rect x="34" y="110" width="80" height="10" rx="3" fill="#eaf0ff" />
      <rect x="34" y="146" width="60" height="8" rx="3" fill="#4b5563" />
      <rect x="34" y="166" width="80" height="8" rx="3" fill="#4b5563" />
      <rect x="156" y="18" width="304" height="164" rx="8" fill="#1f242e" stroke="#3a4150" />
      <rect x="172" y="34" width="150" height="12" rx="3" fill="#c6ccd6" />
      <rect x="172" y="58" width="272" height="8" rx="3" fill="#5b6472" />
      <rect x="172" y="72" width="220" height="8" rx="3" fill="#5b6472" />
    </svg>
  );
}

function EsquemaFormularioUrl() {
  return (
    <svg viewBox="0 0 480 200" className="h-auto w-full" role="img" aria-label="Formulario para configurar la URL de notificaciones y elegir el evento Order">
      <rect x="0" y="0" width="480" height="200" rx="12" fill="#15181f" />
      <rect x="24" y="20" width="180" height="12" rx="3" fill="#c6ccd6" />
      <rect x="24" y="46" width="60" height="8" rx="3" fill="#8b93a1" />
      <rect x="24" y="60" width="432" height="34" rx="7" fill="#1f242e" stroke="#2f60d6" strokeWidth="2" />
      <rect x="36" y="72" width="290" height="10" rx="3" fill="#7ea2f5" />
      <rect x="24" y="108" width="140" height="8" rx="3" fill="#8b93a1" />
      <g>
        <rect x="24" y="122" width="16" height="16" rx="4" fill="#2f60d6" />
        <rect x="48" y="126" width="130" height="8" rx="3" fill="#c6ccd6" />
      </g>
      <g>
        <rect x="24" y="146" width="16" height="16" rx="4" fill="#2a2f3a" stroke="#4b5563" />
        <rect x="48" y="150" width="150" height="8" rx="3" fill="#5b6472" />
      </g>
      <rect x="356" y="168" width="100" height="26" rx="7" fill="#2f60d6" />
      <rect x="374" y="176" width="64" height="10" rx="3" fill="#eaf0ff" />
    </svg>
  );
}

function EsquemaConfirmacion() {
  return (
    <svg viewBox="0 0 480 200" className="h-auto w-full" role="img" aria-label="Pantalla de confirmación con la notificación guardada">
      <rect x="0" y="0" width="480" height="200" rx="12" fill="#15181f" />
      <circle cx="240" cy="70" r="28" fill="#173b2c" stroke="#2c8f6b" strokeWidth="2" />
      <path d="M226 70 L237 81 L256 58" fill="none" stroke="#3ddc97" strokeWidth="5" strokeLinecap="round" strokeLinejoin="round" />
      <rect x="140" y="118" width="200" height="10" rx="3" fill="#c6ccd6" />
      <rect x="170" y="138" width="140" height="8" rx="3" fill="#6b7280" />
    </svg>
  );
}

const PASOS = [
  {
    n: 1,
    titulo: "Entrá a Tus integraciones",
    Esquema: EsquemaListaApps,
    texto: (
      <>
        Entrá a{" "}
        <a
          href="https://www.mercadopago.com.ar/developers/panel/app"
          target="_blank"
          rel="noreferrer"
          className="font-semibold text-brand underline"
        >
          mercadopago.com.ar/developers/panel/app
        </a>{" "}
        con la MISMA cuenta de Mercado Pago que cargaste como Access Token
        en QRcade. Mercado Pago te va a pedir un código por SMS, WhatsApp o
        llamada al celular asociado a la cuenta — es un paso de seguridad
        de ellos, nadie más puede hacer esto por vos (ni siquiera el admin
        de QRcade).
      </>
    ),
  },
  {
    n: 2,
    titulo: "Abrí tu aplicación de QR Code",
    Esquema: EsquemaListaApps,
    texto: (
      <>
        Vas a ver una lista de aplicaciones. Entrá a la que tiene el
        producto <strong>&quot;QR Code&quot;</strong> — es la que se creó
        cuando activaste el cobro con QR en tu cuenta. Si tenés varias
        aplicaciones y no estás seguro cuál es, fijate el nombre del
        producto debajo de cada una.
      </>
    ),
  },
  {
    n: 3,
    titulo: "Buscá \"Webhooks\"",
    Esquema: EsquemaMenuWebhooks,
    texto: (
      <>
        Dentro de la aplicación, en el menú de la izquierda buscá{" "}
        <strong>Webhooks</strong> (a veces figura como &quot;Notificaciones&quot;).
        Hacé click ahí y después en{" "}
        <strong>&quot;Configurar notificaciones&quot;</strong> o
        &quot;Crear notificación&quot;.
      </>
    ),
  },
  {
    n: 4,
    titulo: "Pegá la URL y elegí el evento \"Order\"",
    Esquema: EsquemaFormularioUrl,
    texto: (
      <WebhookUrlStep />
    ),
  },
  {
    n: 5,
    titulo: "Guardá — listo",
    Esquema: EsquemaConfirmacion,
    texto: (
      <>
        Al guardar, Mercado Pago te va a mostrar una &quot;clave
        secreta&quot; — no hace falta que la copies ni la guardes en
        ningún lado para que esto funcione. Con este paso hecho, cada vez
        que alguien pague en tu QR, Mercado Pago le va a avisar a QRcade
        automáticamente y la ficha va a caer sola.
      </>
    ),
  },
];

async function WebhookUrlStep() {
  const hdrs = await headers();
  const host = hdrs.get("host");
  const webhookUrl = host ? `https://${host}/api/mp/webhook` : "https://qrcade.dev/api/mp/webhook";

  return (
    <>
      En el campo de URL pegá exactamente esto:
      <div className="my-2.5 select-all rounded-lg border border-line-soft bg-surface-2 px-3.5 py-2.5 font-mono text-[13px] text-ink">
        {webhookUrl}
      </div>
      Después, en la lista de eventos, tildá{" "}
      <strong>&quot;Order (Mercado Pago)&quot;</strong> — puede aparecer
      traducido como &quot;Órdenes&quot;. No hace falta tildar ningún otro
      evento.
    </>
  );
}

export default function AyudaWebhookMercadoPagoPage() {
  return (
    <div className="mx-auto max-w-[720px]">
      <Link
        href="/dashboard/cuenta"
        className="mb-1.5 block text-xs text-ink-faint hover:text-ink"
      >
        ← Mi cuenta
      </Link>
      <h1 className="mb-1 text-2xl font-bold tracking-tight">
        Configurar el webhook de pagos
      </h1>
      <p className="mb-2 max-w-[560px] text-[13px] text-ink-muted">
        Este paso es lo único que Mercado Pago no te deja automatizar: sin
        esto, tu QR puede mostrar el monto bien pero{" "}
        <strong>ningún pago va a acreditarse solo</strong> (la ficha no va a
        caer). Se hace UNA SOLA VEZ por cuenta de Mercado Pago — no por
        máquina — y toma unos 2 minutos.
      </p>
      <p className="mb-7 max-w-[560px] text-[11px] text-ink-faint">
        Las imágenes de abajo son esquemas de dónde hacer click, no
        capturas reales — el panel de Mercado Pago cambia de tanto en
        tanto y cada cuenta tiene su propio nombre de aplicación.
      </p>

      <div className="space-y-4">
        {PASOS.map(({ n, titulo, Esquema, texto }) => (
          <div
            key={n}
            className="rounded-2xl border border-line bg-surface p-6"
          >
            <div className="mb-3 flex items-center gap-3">
              <div className="flex h-7 w-7 shrink-0 items-center justify-center rounded-full bg-brand text-[13px] font-bold text-white">
                {n}
              </div>
              <div className="text-sm font-bold">{titulo}</div>
            </div>
            <div className="mb-4 overflow-hidden rounded-lg border border-line-soft">
              <Esquema />
            </div>
            <div className="text-[13px] leading-relaxed text-ink-muted">
              {texto}
            </div>
          </div>
        ))}
      </div>

      <div className="mt-6 rounded-2xl border border-warn-line bg-warn-bg p-5 text-[13px] text-warn">
        <strong>¿Ya lo configuraste y seguís sin ver pagos acreditados?</strong>{" "}
        Probá un pago de nuevo y avisale al admin — con la hora exacta del
        intento se puede revisar el log del webhook para ver qué pasó.
      </div>
    </div>
  );
}
