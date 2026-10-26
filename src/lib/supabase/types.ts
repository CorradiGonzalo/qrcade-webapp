export type Role = "admin" | "usuario";

export type DeviceStatus = "unclaimed" | "claimed" | "revoked";

export type DeviceMode = "fijo" | "combo";

export type DeviceKind = "garra" | "cafetera";

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
  kind: DeviceKind;
  folder_id: string | null;
  // Config propia de kind='cafetera' — ver migración 0011. En una
  // 'garra' quedan en sus valores default, sin uso.
  cafe_mp_external_reference: string | null;
  cafe_pulso_min_us: number;
  cafe_off_delay_ms: number;
  cafe_clean_pulse_ms: number;
  cafe_heat_on_min: number;
  cafe_heat_off_min: number;
  cafe_clean_every_n_cycles: number;
  cafe_bt_reset_cmd: string;
  cafe_pulso_nivel_high: boolean;
  // Ver migración 0014 — sólo se usa para que una ESP32 "sólo pantalla"
  // (sin relé) pida el QR vigente sin ser tratada como una placa nueva.
  display_pairing_code: string | null;
  created_at: string;
  claimed_at: string | null;
  revoked_at: string | null;
}

export interface DeviceFolder {
  id: string;
  owner_id: string;
  parent_id: string | null;
  name: string;
  created_at: string;
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
  kind: DeviceKind;
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
  | "restart_requested"
  | "firmware_update"
  | "clean_cycle";

export interface DeviceEvent {
  id: string;
  device_id: string;
  type: DeviceEventType;
  message: string;
  created_at: string;
}
