# Habilidades Random — evento hasta nivel 80 con base propia

Módulo de desarrollo para AzerothCore / WoW 3.3.5a. El evento usa **su propia base de datos** y crea sus tablas y pool desde C++. Por defecto reutiliza las credenciales que el core ya tiene configuradas, mediante una conexión independiente; no pide otra contraseña. No se importa ningún SQL en `world`, `characters` o `auth`.

La versión **2.2.1-dev** conserva las correcciones de persistencia, restauración y cartas, y corrige el arranque: un fallo del evento ya no detiene `worldserver` ni expulsa a personajes ajenos. Sigue pendiente validar esta revisión en el servidor completo y el cliente real antes de usarla en producción.

## Funcionamiento

Un personaje de cualquier clase acepta el desafío en un NPC. Se guardan su clase, hechizos, talentos de ambas especializaciones, glifos y destrezas incompatibles con druida en la base privada. Pasa temporalmente a **druida conservando raza, género y apariencia racial**, obtiene poderes aleatorios y no puede asignar talentos. Profesiones, equitación y utilidades genéricas se conservan.

Al llegar al nivel **80**, se retiran los poderes del evento y se restauran los poderes originales. Puede volver a usar entrenadores; no aprende automáticamente todas las habilidades de nivel 80. Si está muerto, en combate o teletransportándose, la restauración espera una situación segura y se reintenta automáticamente. La copia original se conserva después de terminar. Una participación por personaje; bajar y volver a subir niveles no genera elecciones adicionales.

La clase y las habilidades actuales siguen formando parte del personaje normal: **AzerothCore las guarda mediante sus API habituales**. Esto no crea tablas del evento ni instala consultas personalizadas en las bases del core. Los únicos datos adicionales del modo están en su base privada.

## Requisitos de código

El core necesita permitir el registro de datos de clase y plantillas NPC **en memoria**, y poder suspender el guardado de una sesión cuyo estado quedó incierto. Se incluyen estas extensiones:

- `patches/hollowsource-3927b32-runtime-data.patch`: adaptado a los archivos `server.zip` del fork del usuario.
- `patches/azerothcore-runtime-data.patch`: variante para el core de referencia.
- `patches/player-save-guard.patch`: complemento obligatorio para evitar que el cierre de una sesión vuelva a guardar un personaje después de fallar su transacción.

Las dos variantes de `runtime-data` son alternativas: **no aplicar ambas**. Añaden dos métodos de registro a `ObjectMgr`, sin consultas SQL. Rechazan colisiones y no sustituyen datos existentes. El módulo genera en memoria las combinaciones raza/druida ausentes y su NPC; conserva los perfiles existentes.

El parche `player-save-guard` se aplica además de la variante runtime elegida. Añade una marca temporal a Player para bloquear los guardados completo, de inventario y de oro después de un fallo; el siguiente inicio de sesión crea un Player nuevo sin esa marca. Esto evita que el cierre normal guarde oro actualizado junto a un inventario cuyo guardado se había revertido. No añade columnas ni SQL a las bases del core. Si falta alguna extensión, el evento permanece inactivo y `worldserver` continúa.

El helper de instalación elige el parche que se pueda aplicar y reconoce una instalación ya preparada. Referencia adicional: AzerothCore `9eabf314a27b71d4e9cd513f6f2c08befe90d4ef`. Para otros commits hay que comprobar la compatibilidad. No se usan DBC ni MPQ personalizados.

## Compatibilidad de hooks y recompilación

La apertura de la base privada se hace desde `WorldScript::OnBeforeWorldInitialized`, su keepalive desde `OnUpdate` y su cierre después de descargar los mapas. No se requieren los hooks recientes `OnModuleDatabasesLoading`, `OnModuleDatabasesKeepAlive` o `OnModuleDatabasesClosing`.

La actualización del personaje usa `OnPlayerBeforeUpdate`. El adaptador `HRLearnSpellHook` comprueba la firma exacta de `OnPlayerCanLearnSpell`: si existe, rechaza el aprendizaje antes de realizarlo; si no existe, usa el hook histórico `OnPlayerLearnSpell`, retira el hechizo inmediatamente y corrige el paquete del libro en el siguiente tick. Se mantienen overrides reales en cada ruta.

Durante el modo se bloquean el menú y las solicitudes directas a entrenadores de clase antes de que el core cobre la compra. Los entrenadores de profesiones se conservan. El hook de aprendizaje sigue protegiendo otras fuentes de hechizos normales de clase.

Si compilaste una versión anterior y ves errores C3668 en esos hooks:

1. Si el módulo se clonó como repositorio independiente, actualizar su carpeta con `git pull --ff-only`. Si se copió desde un ZIP, sustituirla por la descarga actualizada conservando la configuración local.
2. Volver a configurar/generar el proyecto con CMake en el directorio de compilación que ya usabas.
3. Recompilar `modules` y `worldserver` en Visual Studio. No quitar `override` manualmente.
4. Ejecutar el helper de preparación: reconoce el parche runtime anterior y añade la protección de guardado que falte. No aplicar manualmente una segunda variante runtime.

Estas rutas eliminan las cinco dependencias incompatibles identificadas en la captura. Las firmas se contrastaron con los archivos reales de `server.zip`, además del core de referencia.

Destino indicado por el usuario: `yafeth950-test/hollowsource`, commit `3927b32566d2910b332decb4fa97e3a2e0879b40`. Se inspeccionó su carpeta `src/server` recibida por ZIP; el hash declarado no se puede verificar a partir de ese ZIP. Las comprobaciones C++ usan esos encabezados del servidor y `src/common`/dependencias del core de referencia, porque esas carpetas no se incluyeron. Falta compilar y enlazar el servidor completo con MSVC antes de certificar compatibilidad en Windows.

Para revisar otro fork sin compartirlo completo, se puede generar un paquete de API desde su raíz:

```text
python modules/mod-habilidades-random/tools/collect_core_compat.py --core . --output hollowsource-compat.zip
```

El paquete contiene solo una lista fija de archivos C++ necesarios y el hash del commit. No incluye configuración, SQL, remotos Git ni credenciales, y no sobrescribe un ZIP existente. Compartir ese archivo permite revisar el fork sin publicar el repositorio completo.

## Instalación

1. Copiar el módulo como `modules/mod-habilidades-random` dentro de AzerothCore.
2. Desde la raíz de AzerothCore, preparar el parche de código (Python 3 y Git en el PATH):

   ```sh
   python modules/mod-habilidades-random/tools/prepare_core.py --core .
   ```

   Se comprueba la aplicación de todos los parches necesarios antes de modificar archivos. Si las dos API ya están completas, no hace cambios. También actualiza cores que ya tenían solo el parche runtime. Si detecta una aplicación parcial o un core incompatible, se detiene sin forzar. Se puede añadir `--check` para revisar sin aplicar.
3. Instalar la configuración del módulo. La conexión es automática usando las credenciales de la base de personajes del core; basta dejar los valores predeterminados:

   ```ini
   HabilidadesRandom.DatabaseName = "mod_habilidades_random"
   HabilidadesRandom.DatabaseInfo = ""
   ```

   No hace falta repetir usuario ni contraseña. La cuenta MySQL que ya usa el core debe tener permiso para crear esa base y administrar sus tablas. Se copia su configuración de conexión sin cambiar la conexión ni el nombre de la base de personajes. Usar una base propia **por reino**, cambiando `DatabaseName` si comparten servidor MySQL.

   Si una instalación anterior ya contiene una cadena en `DatabaseInfo`, vaciarla o quitar esa línea para activar la herencia. Una cadena explícita sigue siendo opcional para otro servidor/usuario: `host;puerto_o_socket;usuario;contraseña;base_privada[;ssl]`; en ese caso, la base de esa cadena tiene prioridad sobre `DatabaseName`. No subir configuraciones con contraseñas a GitHub.
4. Reconfigurar, compilar e instalar `worldserver` con el módulo y reiniciarlo.
5. Al arrancar `worldserver` con el evento habilitado y sus parches disponibles, el módulo crea la base, diez tablas y la pool inicial de 474 IDs. **Compilar no instala las tablas.** Los siguientes arranques conservan las copias y los cambios de la pool; no la reemplazan.
6. Como GM, ir al lugar elegido y ejecutar:

   ```text
   .hr npc
   ```

   El NPC y su posición pertenecen al módulo. No ejecutar `.npc add 900100`: ese comando normal del core escribe spawns en `world`. Para retirar un NPC del evento, seleccionarlo y ejecutar `.hr removenpc`.
7. Copiar `client/addon/HabilidadesRandom` a `Interface/AddOns/HabilidadesRandom` del cliente y activar el addon.

El NPC explica el desafío y pide confirmar la inscripción. Clic izquierdo en una carta: elegir. Clic derecho: bloquear. Reroll cambia la oferta solo si hay tres sustitutos disponibles. Los NPC se pueden colocar en los continentes 0, 1, 530 y 571; sus posiciones se restauran desde la base del evento en cada arranque. No se guardan en instancias ni transportes.

La entrada runtime predeterminada es **900100**, configurable con `HabilidadesRandom.NpcEntry`. Debe estar libre. Si ya existe una plantilla ajena con ese ID, elegir otro; el módulo no la sobrescribe.

## Configuración del evento

El archivo `conf/mod_habilidades_random.conf.dist` está dividido en evento, NPC, elecciones, cambios y bloqueos, rarezas y base propia. Conserva los nombres de las opciones anteriores. No hay que escribir credenciales para la instalación predeterminada.

`InitialDrafts` concede elecciones al inscribirse; `DraftsPerLevel` añade elecciones por niveles nuevos. `InitialRerolls` y `RerollsPerLevel` controlan los cambios de oferta. `InitialBans` es el saldo inicial de bloqueos. Los saldos no se gastan por una oferta incompleta, repetida o sin sustitutos disponibles, y bajar/subir el mismo nivel no genera premios adicionales.

Las opciones `Distribution.*` son pesos relativos: pueden expresarse como `0.50`/`0.27` o `50`/`27`, y cero excluye una categoría. Se elige sin reemplazo y sin redondear a cero las categorías poco frecuentes. `PoolAmount` limita el conjunto temporal de candidatos antes de preparar las cartas. La categoría `Broken` requiere además `IncludeRarity5 = 1`. Si no existen tres candidatos elegibles, la inscripción no se inicia; durante un evento ya iniciado las elecciones se conservan para otro nivel.

Después de actualizar esta versión, reemplazar también el addon del cliente: los bloqueos se sincronizan en fragmentos para respetar el tamaño de los mensajes.

## Separación de datos y recuperación

La base privada contiene progreso, ofertas, saldos, bloqueos, propiedad de poderes, copias originales, versión de esquema y posiciones NPC. La conexión rechaza nombres de las bases del core y nombres de sistema. No hay archivos `data/sql/` que un actualizador pueda importar accidentalmente en `world` o `characters`.

El esquema v2 añade la copia de destrezas que el core descarta al reconectar como druida, por ejemplo espadas o arcos de la clase original. Se guardan sus puntos, máximo y paso al inscribirse y se recuperan al terminar; no se retrocede el progreso de profesiones. La migración desde el esquema v1 es automática y aditiva. Las copias v1 permiten devolver clase y poderes, pero no pueden reconstruir puntos de destreza perdidos antes de existir esa copia; se avisa al restaurar.

Los guardados se hacen en dos pasos: primero se confirma el estado en la base privada y se marca `character_pending`; después se ejecuta el guardado generado por `Player::SaveToDB` y las sentencias preparadas del core. Una conexión propia comprueba `START TRANSACTION`, cada operación y `COMMIT`, sin reconectar y repetir una consulta a mitad de transacción. Esto evita depender del resultado incompleto de `ExecuteTransaction` del fork. No crea tablas ni consultas personalizadas de personajes. Solo al confirmar el guardado se quita la marca.

No se presupone una transacción atómica entre dos bases o servidores distintos. Si una escritura privada o el guardado del personaje quedan sin confirmar, se detienen las acciones, se suspenden los guardados de esa sesión y se cierra para recuperar desde los datos durables al conectar. Un guardado del personaje pendiente se reconstruye con su proyección completa de hechizos, talentos, glifos y destrezas. También se conserva el nivel 80 cuando su restauración quedó pendiente. Los talentos permanecen bloqueados hasta confirmar la restauración del personaje.

Al inicializar se carga y valida un índice liviano de inscripciones. Los personajes ajenos y los participantes ya completados con su clase original entran sin consultar la base del evento. Si falla la base, solo se aísla una inscripción confirmada que todavía necesita recuperación o una conversión inequívoca; no se expulsa a todos los jugadores. Una conversión sin copia válida también se bloquea hasta recuperar la base correcta.

Los guardados confirmados son síncronos desde el punto de vista del hilo de juego. Se debe medir la latencia con la carga esperada antes de abrir un evento concurrido.

### Arranque y desactivación

Con `HabilidadesRandom.Enable = 0` desde el arranque no se abre la base privada ni se preparan o colocan NPC. Si están los parches, solo se registran los perfiles de raza/druida en memoria para que los personajes convertidos sigan siendo reconocibles por el core. Si faltan los parches o falla la base, el evento queda inactivo y el servidor continúa; se registra el motivo.

Cambiar a cero mediante una recarga de configuración cuando el evento ya estaba funcionando conserva la conexión y el índice existentes: se cierran inscripciones y se restauran participantes cuando sea seguro. Si falla el NPC pero la base y los perfiles están disponibles, también se conserva la recuperación. Activar tras un arranque deshabilitado, o después de corregir un fallo de inicialización, requiere reiniciar `worldserver`; no se inyectan perfiles ni NPC en caliente.

Sin abrir la base no se pueden reconocer todas las partidas pendientes: una conversión a druida de una raza normalmente druida no se distingue de un druida normal. Por eso, restaurar los participantes con el evento todavía operativo antes de apagarlo desde el arranque. No retirar el módulo, su base ni el parche mientras queden participantes sin restaurar, ni borrar copias para permitir otra inscripción.

## Si ya instalaste la versión anterior

**No actualizar con personajes todavía dentro del desafío anterior.** Primero restaurarlos usando esa versión y guardar una copia de sus bases. Esta versión no lee, migra ni elimina tablas del evento que se hayan instalado previamente en `characters`, ni filas antiguas de `world`. La ausencia de esos cambios en la nueva versión no deshace SQL ya ejecutado.

Si el NPC antiguo 900100 permanece en `world`, configurar otra entrada para el NPC runtime, por ejemplo 900101. La limpieza de los datos antiguos debe revisarse por separado con copias de seguridad; no se automatiza una eliminación destructiva.

## Límites de las habilidades

Maná, ira y energía usan capacidades y regeneración normales; no se rellenan infinitamente. Se excluyen recursos incompatibles, como runas/poder rúnico y foco, y efectos que el core impide usar como druida, como domar bestias. Llamar a una mascota requiere una mascota guardada compatible. Formas, sigilo, posturas, armas, mascotas y otras dependencias todavía requieren validación y ajuste de la pool. No se promete que los 474 IDs sean utilizables en cualquier combinación. El equipo está sujeto a las restricciones de druida durante el evento. La mascota original se guarda al entrar y puede requerir volver a invocarla o usar el establo después.

Las cartas pueden incluir talentos activos oficiales; el bloqueo se refiere al aprendizaje y uso de los árboles de talentos del personaje.

## Pruebas locales

Ver `VALIDACION.md` para resultados y límites. Desde la carpeta del módulo:

```sh
g++ -std=c++17 -Wall -Wextra -Werror tests/test_draft_state.cpp -o /tmp/hr-state-tests
/tmp/hr-state-tests
g++ -std=c++17 -Wall -Wextra -Werror tests/test_database_schema.cpp -o /tmp/hr-schema-tests
/tmp/hr-schema-tests
g++ -std=c++17 -Wall -Wextra -Werror tests/test_database_connection.cpp -o /tmp/hr-connection-tests
/tmp/hr-connection-tests
g++ -std=c++17 -Wall -Wextra -Werror tests/test_learn_spell_hooks.cpp -o /tmp/hr-hook-tests
/tmp/hr-hook-tests
g++ -std=c++17 -Wall -Wextra -Werror tests/test_snapshot_rules.cpp -o /tmp/hr-snapshot-tests
/tmp/hr-snapshot-tests
g++ -std=c++17 -Wall -Wextra -Werror -Itests/runtime_stubs tests/test_runtime.cpp -o /tmp/hr-runtime-tests
/tmp/hr-runtime-tests
luac5.1 -p client/addon/HabilidadesRandom/SpellChoice.lua
lua5.1 tests/test_addon.lua
python -m unittest discover -s tests -p 'test_*core*.py'
python tests/test_recovery_save.py
python tests/test_startup_lifecycle.py
python3 tests/test_sql.py --socket /ruta/al/socket-de-pruebas
```

El test SQL crea y borra una base desechable con nombre aleatorio. Ejecutarlo únicamente en un servidor de ensayo con autenticación local y permisos para crear bases.

## Autoría

Basado en el módulo original de Hollow / Prestige & Draft Mode incluido en el ZIP del usuario. Se conserva la licencia MIT suministrada. El parche de integración se aplica a archivos de AzerothCore bajo la licencia y autoría de ese proyecto.
