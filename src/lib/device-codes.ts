import { randomInt } from "crypto";

// Sin caracteres ambiguos (0/O, 1/I/L) para que sea fácil de leer en una
// pantalla de 240x240px y de tipear a mano.
const ALPHABET = "ABCDEFGHJKMNPQRSTUVWXYZ23456789";

/** Código de 6 caracteres para que el dueño vincule su ESP desde la app. */
export function generateClaimCode(length = 6): string {
  let code = "";
  for (let i = 0; i < length; i++) {
    code += ALPHABET[randomInt(0, ALPHABET.length)];
  }
  return code;
}

/** Código de 8 caracteres para que una ESP32 "sólo pantalla" (sin relé)
 * pida el QR vigente de una máquina ya reclamada — ver migración 0014.
 * Un poco más largo que el claim_code porque este vive indefinidamente
 * (no se consume al usarlo una vez), así que conviene algo más difícil
 * de adivinar por las dudas. */
export function generateDisplayPairingCode(length = 8): string {
  let code = "";
  for (let i = 0; i < length; i++) {
    code += ALPHABET[randomInt(0, ALPHABET.length)];
  }
  return code;
}

/** Contraseña temporal legible para la primera entrega al usuario. */
export function generateTemporaryPassword(): string {
  const words = ALPHABET.replace(/[0-9]/g, "");
  let pass = "";
  for (let i = 0; i < 8; i++) {
    pass += words[randomInt(0, words.length)];
  }
  return pass + randomInt(10, 99);
}
