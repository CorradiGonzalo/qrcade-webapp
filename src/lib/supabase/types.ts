export type Role = "admin" | "usuario";

export type DeviceStatus = "unclaimed" | "claimed" | "revoked";

export type DeviceMode = "fijo" | "combo";

export type FirmwareStatus =
  | "notified"
  | "accepted"
  | "downloading"
  | "updated"
  | "dismissed"
  | "failed";

export interface Profile {
  id: string;
  username: string;
  role: Role;
  must_change_password: boolean;
  recovery_email: string | null;
  display_name: string | null;
  mp_access_token: string | null;
  mp_user_id: string | null;
  created_at: string;
}

export interface Device {
  id: string;
  mac: string;
  claim_code: string | null;
  alias: string | null;
  owner_id: string | null;
  status: DeviceStatus;
  firmware_version: string | null;
  last_seen_at: string | null;
  wifi_rssi: number | null;
  is_paused: boolean;
  mode: DeviceMode;
  local_name: string | null;
  caja_name: string | null;
  mp_monto_fijo: number | null;
  store_id: string | null;
  pos_id: string | null;
  qr_data: string | null;
  provisioned_at: string | null;
  created_at: string;
  claimed_at: string | null;
  revoked_at: string | null;
}

export interface DeviceFichaCombo {
  id: string;
  device_id: string;
  fichas: number;
  monto: number;
  created_at: string;
}

export interface FirmwareVersion {
  id: string;
  version: string;
  bin_url: string;
  changelog: string | null;
  created_by: string | null;
  created_at: string;
}

export interface DeviceFirmwareStatus {
  device_id: string;
  firmware_version_id: string;
  status: FirmwareStatus;
  updated_at: string;
}

export type DeviceEventType =
  | "wifi_reconnect"
  | "dispense_test"
  | "dispense_payment"
  | "restart_requested";

export interface DeviceEvent {
  id: string;
  device_id: string;
  type: DeviceEventType;
  message: string;
  created_at: string;
}
