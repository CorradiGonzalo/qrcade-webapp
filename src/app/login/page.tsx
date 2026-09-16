import { LoginForm } from "@/components/LoginForm";

export default function LoginPage() {
  return (
    <main className="flex min-h-screen items-center justify-center bg-bg px-4">
      <div className="w-full max-w-[420px] rounded-[20px] border border-line-soft bg-surface p-10 shadow-2xl">
        <div className="mb-2 flex items-center gap-2.5">
          <div className="flex h-[34px] w-[34px] items-center justify-center rounded-[9px] bg-brand">
            <div className="h-3.5 w-3.5 rounded-sm border-2 border-[#dbe6ff]" />
          </div>
          <div className="text-xl font-bold tracking-tight text-ink">
            QRcade
          </div>
        </div>
        <p className="mb-8 text-sm text-ink-muted">
          Panel de administración de máquinas
        </p>

        <LoginForm />
      </div>
    </main>
  );
}
