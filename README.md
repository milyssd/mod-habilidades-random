# Habilidades Random — desafío temporal hasta nivel 80

Módulo para AzerothCore / WoW 3.3.5a. Versión de desarrollo para pruebas en un servidor de ensayo.

## Reglas del evento

- Un personaje de cualquier clase acepta el desafío hablando con el NPC. No se inscribe automáticamente a nadie.
- Antes de modificarlo, se guarda una copia persistente de su clase, habilidades, talentos de ambas especializaciones y glifos.
- Pasa temporalmente a clase **druida**. Su raza, género y modelos raciales no se cambian: un humano sigue siendo humano.
- Se suspenden las habilidades de clase originales y los talentos. Profesiones, equitación y utilidades genéricas se conservan.
- Recibe una elección inicial y una más por nivel, configurables. Cada oferta tiene tres poderes oficiales de la pool SQL.
- No puede aprender talentos durante el desafío, tampoco cambiando de especialización. Los glifos originales se guardan y sus efectos se suspenden.
- Maná, ira y energía están habilitados mediante el hook de recursos de AzerothCore. No se rellenan infinitamente cada tick.
- Al llegar al **nivel 80**, se retiran los poderes del evento y se restauran su clase, habilidades, talentos y glifos originales. Después puede aprender en entrenadores; no recibe automáticamente todas las habilidades de nivel 80.
- Si llega al 80 en combate, muerto o durante un teletransporte, la restauración espera a que esté vivo y en una situación segura. Se reintenta automáticamente cada segundo. Mientras tanto no se permite aprender talentos de druida.
- Las desconexiones conservan el progreso. La copia original se mantiene incluso después de completar el evento.
- Una participación por personaje. Bajar y volver a subir niveles no proporciona elecciones extra ni elimina la copia original.

## Instalación en un servidor de ensayo

1. Hacer copia de las bases de datos y detener `worldserver` antes de sustituir una versión instalada.
2. Copiar esta carpeta, conservando el nombre `mod-habilidades-random`, dentro de `modules/`.
3. En una instalación nueva, aplicar `data/sql/db-characters/base/mod_habilidades_random.sql` en `acore_characters`.
   Este archivo sustituye la pool. En una actualización que ya tiene esa pool instalada, no es necesario repetirlo.
4. Aplicar **también** `data/sql/db-characters/base/event_schema.sql` en `acore_characters`.
   Esta migración es aditiva y repetible; no sustituye las copias de personajes existentes.
5. Revisar que la entrada de criatura **900100** esté libre. Aplicar `data/sql/db-world/base/event_npc.sql` en `acore_world`.
   Si se usa otra entrada, cambiarla en ambos INSERT. No se sobrescriben criaturas ajenas existentes.
6. Reconfigurar y compilar AzerothCore con el módulo. El core detecta `src/` y su loader automáticamente.
7. Instalar `conf/mod_habilidades_random.conf.dist` como configuración del módulo y revisar los valores.
8. **Reiniciar** `worldserver`: el SQL de mundo añade las combinaciones raza/druida necesarias para reconectar sin cambiar de raza.
   Los datos de nivel se obtienen del sistema actual de estadísticas de clase y modificadores raciales de AzerothCore.
9. Crear el NPC en el lugar elegido con `.npc add 900100`.
10. Copiar `client/addon/HabilidadesRandom` a `Interface/AddOns/HabilidadesRandom` del cliente y activarlo.

El NPC explica el modo y pide aceptar. Clic izquierdo en una carta: elegir. Clic derecho: bloquear. El botón Reroll cambia la oferta si hay tres sustitutos; no cobra cuando no puede generarlos.

Las filas adicionales de `playercreateinfo` permiten cargar personajes inscritos de cualquier raza como druidas. El módulo impide crear directamente druidas de razas no nativas mediante un cliente modificado. No se instalan DBC ni MPQ personalizados.

## Actualizaciones y recuperación

- Los personajes con registros de poderes de la versión antigua no se inscriben automáticamente ni se usan como una supuesta copia original. Su inscripción se rechaza para no mezclar poderes heredados con habilidades normales. Requieren revisión y migración por separado.
- No borrar ni vaciar `mod_habilidades_random_event` o `mod_habilidades_random_snapshot`.
- Una pool vacía/inaccesible cierra las nuevas inscripciones. No se usa como motivo para borrar habilidades. Los participantes con copia válida se restauran de forma segura.
- Desactivar `HabilidadesRandom.Enable` cierra las inscripciones y restaura a los participantes conectados cuando sea seguro; los desconectados se restauran al volver a entrar.
- **No retirar el módulo ni las combinaciones raza/druida mientras haya participantes sin restaurar.** Verificar primero `SELECT COUNT(*) FROM mod_habilidades_random_event WHERE completed=0;`. Un personaje desconectado conserva su clase temporal hasta que vuelve a entrar y se restaura.
- Los guardados de selección incluyen saldos, oferta, bans y propiedad de hechizos en una misma transacción. Se confirma su resultado antes de publicar el nuevo estado. Las transiciones también guardan al personaje. Se usan los workers de AzerothCore porque sus sentencias de guardado son preparadas para conexiones asíncronas.
- Se espera la confirmación del guardado en el hilo del mundo. Es una limitación de rendimiento de esta primera versión: medir la latencia y carga con el número real de participantes antes de un evento concurrido.

## Límites de las habilidades multiclase

La clase druida resuelve la disponibilidad de maná/ira/energía, pero no convierte automáticamente todas las habilidades en utilizables. Se excluyen poderes con recursos incompatibles, como runas/poder rúnico y foco. Formas, sigilo, posturas, armas, mascotas y dependencias entre poderes siguen requiriendo validación y ajuste de la pool. No se promete que los 474 IDs sean utilizables en cualquier combinación. El equipo queda sujeto a las restricciones de druida durante el evento; conviene entrar con equipo compatible. La mascota original se guarda/dismiss al entrar y puede requerir volver a invocarla o usar el establo al completar el modo.

Los talentos activos obtenidos como cartas siguen siendo poderes aleatorios; el bloqueo se refiere a aprender/asignar puntos en los árboles de talentos.

## Validación realizada

Ver `VALIDACION.md` para resultados y límites. La comprobación de C++ contra encabezados reales no equivale a enlazar un worldserver ni a probar el ciclo con cliente y datos de juego.

Pruebas locales desde la carpeta del módulo:

```sh
g++ -std=c++17 -Wall -Wextra -Werror tests/test_draft_state.cpp -o /tmp/hr-state-tests
/tmp/hr-state-tests
luac5.1 -p client/addon/HabilidadesRandom/SpellChoice.lua
lua5.1 tests/test_addon.lua
python3 tests/test_sql.py --socket /ruta/al/socket-de-pruebas --core /ruta/a/azerothcore
```

El test SQL crea una base desechable con nombre aleatorio, prueba las migraciones contra esquemas reales de AzerothCore y la borra al terminar. Debe usarse un servidor de base de datos de ensayo con autenticación local y permisos para crear bases.

## Autoría

Basado en el módulo original de Hollow / Prestige & Draft Mode incluido en el ZIP del usuario. Se conserva la licencia MIT suministrada.
