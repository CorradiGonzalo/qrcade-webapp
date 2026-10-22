import { redirect } from "next/navigation";
import { requireActiveUser } from "@/lib/supabase/auth";
import { UserSidebar } from "@/components/UserSidebar";

export default async function DashboardLayout({
  children,
}: {
  children: React.ReactNode;
}) {
  const { profile } = await requireActiveUser();

  if (profile.role === "admin") redirect("/admin/equipos");

  return (
    <div className="flex min-h-screen flex-col bg-bg md:flex-row">
      <UserSidebar displayName={profile.display_name ?? profile.username} />
      <div className="min-w-0 flex-1 overflow-x-hidden px-4 py-5 sm:px-6 md:px-10 md:py-8">
        {children}
      </div>
    </div>
  );
}
