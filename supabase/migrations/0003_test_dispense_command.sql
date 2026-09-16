-- Agrega "test_dispense" a los comandos válidos: un botón en el panel
-- para disparar el relé una vez y probar que la máquina anda, sin
-- necesitar el cable serial (reemplaza al viejo comando 'c' por Arduino
-- IDE). No afecta ventas ni fichas — es sólo una prueba física.

alter table public.device_commands
  drop constraint if exists device_commands_command_check;

alter table public.device_commands
  add constraint device_commands_command_check
  check (command in ('restart', 'test_dispense'));
