import { requireAdmin } from "@/lib/supabase/auth";
import { AdminSidebar } from "@/components/AdminSidebar";

export default async function AdminLayout({
  children,
}: {
  children: React.ReactNode;
}) {
  const { profile } = await requireAdmin();

  return (
    <div className="flex min-h-screen flex-col bg-bg md:flex-row">
      <AdminSidebar displayName={profile.display_name ?? profile.username} />
      <div className="min-w-0 flex-1 overflow-x-hidden px-4 py-5 sm:px-6 md:px-10 md:py-8">
        {children}
      </div>
    </div>
  );
}
