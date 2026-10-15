# Habilidades Random — evento hasta nivel 80 con base propia

Módulo de desarrollo para AzerothCore / WoW 3.3.5a. El evento usa **su propia base de datos** y crea sus tablas y pool desde C++. No se importa ningún SQL en `world`, `characters` o `auth`.

## Funcionamiento

Un personaje de cualquier clase acepta el desafío en un NPC. Se guardan su clase, habilidades, talentos de ambas especializaciones y glifos en la base privada. Pasa temporalmente a **druida conservando raza, género y apariencia racial**, obtiene poderes aleatorios y no puede asignar talentos. Profesiones, equitación y utilidades genéricas se conservan.

Al llegar al nivel **80**, se retiran los poderes del evento y se restauran los poderes originales. Puede volver a usar entrenadores; no aprende automáticamente todas las habilidades de nivel 80. Si está muerto, en combate o teletransportándose, la restauración espera una situación segura y se reintenta automáticamente. La copia original se conserva después de terminar. Una participación por personaje; bajar y volver a subir niveles no genera elecciones adicionales.

La clase y las habilidades actuales siguen formando parte del personaje normal: **AzerothCore las guarda mediante sus API habituales**. Esto no crea tablas del evento ni instala consultas personalizadas en las bases del core. Los únicos datos adicionales del modo están en su base privada.

## Requisitos de código

El core necesita permitir el registro de datos de clase y plantillas NPC **en memoria**. La API estándar del commit de referencia no expone ambas operaciones. Se incluye una extensión pequeña y acotada:

`patches/azerothcore-runtime-data.patch`

Añade dos métodos de registro a `ObjectMgr`, sin consultas SQL. Rechaza colisiones y no sustituye datos existentes. El módulo genera en memoria las combinaciones raza/druida ausentes y su NPC. Los perfiles originales de raza/clase permanecen intactos. Sin esta extensión el módulo rechaza el arranque, en lugar de recurrir a SQL de `world`.

Referencia comprobada: AzerothCore `9eabf314a27b71d4e9cd513f6f2c08befe90d4ef`. Para otros commits hay que comprobar la aplicación y compatibilidad del parche. No se usan DBC ni MPQ personalizados.

## Compatibilidad de hooks y recompilación

La apertura de la base privada se hace desde `WorldScript::OnBeforeWorldInitialized`, su keepalive desde `OnUpdate` y su cierre después de descargar los mapas. No se requieren los hooks recientes `OnModuleDatabasesLoading`, `OnModuleDatabasesKeepAlive` o `OnModuleDatabasesClosing`.

La actualización del personaje usa `OnPlayerBeforeUpdate`. El adaptador `HRLearnSpellHook` comprueba la firma exacta de `OnPlayerCanLearnSpell`: si existe, rechaza el aprendizaje antes de realizarlo; si no existe, usa el hook histórico `OnPlayerLearnSpell`, retira el hechizo inmediatamente y corrige el paquete del libro en el siguiente tick. Se mantienen overrides reales en cada ruta.

Durante el modo se bloquea el menú de entrenadores de clase; los entrenadores de profesiones se conservan. En un core con el hook antiguo, un intento de entrenamiento enviado directamente por un cliente modificado todavía puede cobrar dinero antes de que se retire el hechizo. No se promete reembolso automático en esa ruta.

Si compilaste una versión anterior y ves errores C3668 en esos hooks:

1. Si el módulo se clonó como repositorio independiente, actualizar su carpeta con `git pull --ff-only`. Si se copió desde un ZIP, sustituirla por la descarga actualizada conservando la configuración local.
2. Volver a configurar/generar el proyecto con CMake en el directorio de compilación que ya usabas.
3. Recompilar `modules` y `worldserver` en Visual Studio. No quitar `override` manualmente.
4. El parche runtime sigue siendo necesario. Si ya está aplicado, no volver a aplicarlo.

Estas rutas eliminan las cinco dependencias incompatibles identificadas en la captura. Se comprobaron con los encabezados de referencia y una copia en la que faltan esos cinco métodos; no equivale a compilar todas las versiones de AzerothCore.

El fork indicado por el usuario es `yafeth950-test/hollowsource`, commit `3927b32566d2910b332decb4fa97e3a2e0879b40`. No fue accesible desde este entorno (`Repository not found`). Si es privado, se puede generar un paquete pequeño para adaptar su API, ejecutando desde la raíz del core:

```text
python modules/mod-habilidades-random/tools/collect_core_compat.py --core . --output hollowsource-compat.zip
```

El paquete contiene solo una lista fija de archivos C++ necesarios y el hash del commit. No incluye configuración, SQL, remotos Git ni credenciales, y no sobrescribe un ZIP existente. Compartir ese archivo permite revisar el fork sin publicar el repositorio completo.

## Instalación

1. Copiar el módulo como `modules/mod-habilidades-random` dentro de AzerothCore.
2. Desde la raíz de AzerothCore, comprobar y aplicar el parche de código:

   ```sh
   git apply --check modules/mod-habilidades-random/patches/azerothcore-runtime-data.patch
   git apply modules/mod-habilidades-random/patches/azerothcore-runtime-data.patch
   ```

   Si ya está aplicado, no repetirlo. Si falla la comprobación, adaptar el parche al commit de destino; no forzar su aplicación ni reiniciar sobre un core incompatible.
3. Instalar la configuración del módulo y completar **localmente** `HabilidadesRandom.DatabaseInfo`:

   ```ini
   HabilidadesRandom.DatabaseInfo = "127.0.0.1;3306;hr_event;CONTRASEÑA_LOCAL;mod_habilidades_random"
   ```

   Formato del core: `host;puerto_o_socket;usuario;contraseña;base_privada[;ssl]`. Usar una base propia **por reino**. La cuenta MySQL debe existir y tener permisos para crear esa base y administrar únicamente sus tablas. No necesita permisos sobre las bases del core. No subir la configuración con contraseñas a GitHub.
4. Reconfigurar, compilar e instalar `worldserver` con el módulo y reiniciarlo.
5. Al arrancar, el módulo crea la base, ocho tablas y la pool inicial de 474 IDs. Los siguientes arranques conservan las copias y los cambios de la pool; no la reemplazan.
6. Como GM, ir al lugar elegido y ejecutar:

   ```text
   .hr npc
   ```

   El NPC y su posición pertenecen al módulo. No ejecutar `.npc add 900100`: ese comando normal del core escribe spawns en `world`. Para retirar un NPC del evento, seleccionarlo y ejecutar `.hr removenpc`.
7. Copiar `client/addon/HabilidadesRandom` a `Interface/AddOns/HabilidadesRandom` del cliente y activar el addon.

El NPC explica el desafío y pide confirmar la inscripción. Clic izquierdo en una carta: elegir. Clic derecho: bloquear. Reroll cambia la oferta solo si hay tres sustitutos disponibles. Los NPC se pueden colocar en los continentes 0, 1, 530 y 571; sus posiciones se restauran desde la base del evento en cada arranque. No se guardan en instancias ni transportes.

La entrada runtime predeterminada es **900100**, configurable con `HabilidadesRandom.NpcEntry`. Debe estar libre. Si ya existe una plantilla ajena con ese ID, elegir otro; el módulo no la sobrescribe.

## Separación de datos y recuperación

La base privada contiene progreso, ofertas, saldos, bloqueos, propiedad de poderes, copias originales, versión de esquema y posiciones NPC. La conexión rechaza nombres de las bases del core y nombres de sistema. No hay archivos `data/sql/` que un actualizador pueda importar accidentalmente en `world` o `characters`.

Los guardados se hacen en dos pasos: primero se confirma el estado en la base privada y se marca `character_pending`; después se llama al guardado normal de AzerothCore. Solo al confirmarlo se quita la marca. No se presupone una transacción atómica entre dos bases o servidores distintos. Si falla un paso, la copia y el estado deseado permiten reintentar y reconciliar al conectar. También se conserva el nivel 80 cuando su restauración quedó pendiente. Los talentos permanecen bloqueados hasta confirmar la restauración del personaje.

Si la base privada no se puede consultar, se rechaza temporalmente el inicio de sesión para no interpretar un fallo como ausencia de inscripción. Una conversión sin copia válida también se bloquea hasta recuperar la base correcta.

Los guardados confirmados son síncronos desde el punto de vista del hilo de juego. Se debe medir la latencia con la carga esperada antes de abrir un evento concurrido.

Desactivar `HabilidadesRandom.Enable` cierra la inscripción y restaura participantes cuando sea seguro. No retirar el módulo, su base ni el parche mientras queden participantes sin restaurar. Los desconectados se restauran al volver a entrar. No borrar las copias para permitir una nueva inscripción.

## Si ya instalaste la versión anterior

**No actualizar con personajes todavía dentro del desafío anterior.** Primero restaurarlos usando esa versión y guardar una copia de sus bases. Esta versión no lee, migra ni elimina tablas del evento que se hayan instalado previamente en `characters`, ni filas antiguas de `world`. La ausencia de esos cambios en la nueva versión no deshace SQL ya ejecutado.

Si el NPC antiguo 900100 permanece en `world`, configurar otra entrada para el NPC runtime, por ejemplo 900101. La limpieza de los datos antiguos debe revisarse por separado con copias de seguridad; no se automatiza una eliminación destructiva.

## Límites de las habilidades

Maná, ira y energía usan capacidades y regeneración normales; no se rellenan infinitamente. Se excluyen recursos incompatibles, como runas/poder rúnico y foco. Formas, sigilo, posturas, armas, mascotas y otras dependencias todavía requieren validación y ajuste de la pool. No se promete que los 474 IDs sean utilizables en cualquier combinación. El equipo está sujeto a las restricciones de druida durante el evento. La mascota original se guarda al entrar y puede requerir volver a invocarla o usar el establo después.

Las cartas pueden incluir talentos activos oficiales; el bloqueo se refiere al aprendizaje y uso de los árboles de talentos del personaje.

## Pruebas locales

Ver `VALIDACION.md` para resultados y límites. Desde la carpeta del módulo:

```sh
g++ -std=c++17 -Wall -Wextra -Werror tests/test_draft_state.cpp -o /tmp/hr-state-tests
/tmp/hr-state-tests
g++ -std=c++17 -Wall -Wextra -Werror tests/test_database_schema.cpp -o /tmp/hr-schema-tests
/tmp/hr-schema-tests
g++ -std=c++17 -Wall -Wextra -Werror tests/test_learn_spell_hooks.cpp -o /tmp/hr-hook-tests
/tmp/hr-hook-tests
luac5.1 -p client/addon/HabilidadesRandom/SpellChoice.lua
lua5.1 tests/test_addon.lua
python3 tests/test_sql.py --socket /ruta/al/socket-de-pruebas
```

El test SQL crea y borra una base desechable con nombre aleatorio. Ejecutarlo únicamente en un servidor de ensayo con autenticación local y permisos para crear bases.

## Autoría

Basado en el módulo original de Hollow / Prestige & Draft Mode incluido en el ZIP del usuario. Se conserva la licencia MIT suministrada. El parche de integración se aplica a archivos de AzerothCore bajo la licencia y autoría de ese proyecto.
