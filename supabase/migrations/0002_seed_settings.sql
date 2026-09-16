-- Semilla de configuración. Reemplazá el valor antes de correr en producción:
-- generá una clave de fábrica larga y aleatoria, guardala vos (va embebida
-- en el firmware que compilás) y pegá acá el hash SHA-256 en hex minúscula.
--
-- Generarla, por ejemplo:
--   openssl rand -hex 24            # esto es la clave -> va en el firmware
--   echo -n "LA_CLAVE_DE_ARRIBA" | openssl dgst -sha256   # esto va abajo

insert into public.app_settings (key, value)
values ('factory_key_hash', 'REEMPLAZAR_CON_EL_HASH_SHA256_DE_TU_CLAVE_DE_FABRICA')
on conflict (key) do nothing;
