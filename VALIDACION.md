# Validación de 2.2.0-dev

Destino: carpeta `src/server` del `server.zip` recibido del usuario, atribuida por él a `yafeth950-test/hollowsource` `3927b32566d2910b332decb4fa97e3a2e0879b40`. El ZIP no permite verificar ese hash Git. Para las comprobaciones C++ se emplean sus encabezados reales y `src/common`/dependencias de AzerothCore `9eabf314a27b71d4e9cd513f6f2c08befe90d4ef`, porque esas carpetas no estaban incluidas.

## Correcciones revisadas

- Configuración en español organizada por evento, NPC, elecciones, cambios/bloqueos, rarezas y base propia. Se conservan las claves anteriores y la conexión automática sin repetir credenciales.
- Restauración de talentos usando las máscaras reales de ambas especializaciones, en lugar de `HasTalent`, que ignora el argumento de especialización en el fork.
- Guardados del evento y del personaje con START, operaciones y COMMIT comprobados. No se reconecta para repetir una consulta dentro de una transacción. La conexión de personajes ejecuta el guardado generado por las API y las sentencias preparadas del core; no instala SQL ni tablas del módulo en sus bases.
- Resultado de escritura incierto: se bloquean nuevas acciones y se cierra la sesión. La reconexión recupera la copia durable y el journal pendiente, sin sobrescribirlos desde un estado local anterior.
- El parche `player-save-guard` suspende los guardados completo, de inventario y de oro antes del cierre de una sesión incierta. Evita combinar oro nuevo con un inventario cuyo guardado falló; la marca no se persiste y el siguiente Player comienza sin ella.
- Copia de destrezas, máximo y paso; reconstrucción completa de su proyección al guardar. No se retrocede el progreso de profesiones compartidas. Migración aditiva v1→v2 sin inventar destrezas históricas.
- Base privada creada solo cuando no existe. Tablas meta/NPC con InnoDB y conversión de las versiones anteriores que heredaban otro motor. Se rechazan versiones futuras antes de modificar el esquema.
- Ofertas completas y únicas antes de gastar saldos; premios por niveles sin repetir niveles anteriores ni desbordar contadores; pesos relativos sin truncar rarezas pequeñas. Peso cero excluye una categoría incluso al revalidar ofertas después de recargar configuración.
- Exclusión de efectos incompatibles con el druida temporal, como domar bestias o activar runas. Las llamadas a mascotas exigen una mascota guardada apropiada.
- NPC identificado por mapa y GUID, inserción y obtención de ID bajo la misma transacción y mutex, conservación de posiciones y manejo de fallos de borrado/creación. Se respetan perfiles druidas personalizados que ya existieran en el core.
- Solicitudes directas a entrenadores de clase bloqueadas antes del cobro, además del menú. Se conservan entrenadores de profesiones.
- Addon: solicitudes y temporizadores invalidados correctamente, actualización de rarezas, bloqueos fragmentados, validación de mensajes y remitente, cartas sin zonas de clic superpuestas y textos en español.
- Recopilador de API: lista fija de fuentes, sin seguir enlaces simbólicos de archivos o directorios hacia otros contenidos, y sin sobrescribir ZIP existentes.

## Comprobaciones

- C++17 con `-Wall -Wextra -Werror`: reglas de cartas/premios/pesos; esquema y nombres privados; copia de conexión, puertos, socket, SSL y autenticación; fallos de START, escrituras y COMMIT sin reintento parcial.
- Regresión de restauración de talentos con una API doble que reproduce `HasTalent` ignorando la especialización; especializaciones compartidas/inactivas y fallos de restauración. No sustituye ejecutar `Player` real.
- Runtime de producción compilado con dobles de mapas/base/core: reproduce el mismo GUID en mapas distintos y verifica borrado correcto, errores de DB/spawn, perfiles y recuperación de posiciones.
- Lua 5.1: sintaxis y ejecución del addon real con dobles de API WoW; mensajes, saldos, remitentes, respuestas parciales, temporizadores, cierre y reapertura. XML parseado y geometría comparada con el área visible de la textura; no es renderizado dentro de WoW.
- MariaDB 11.8.6 desechable: DDL extraído del encabezado real, creación/reinicio, pool de 474 IDs sin reemplazar ajustes, copias de especializaciones/glifos/destrezas, migración, motores transaccionales, rollback, journal y posiciones NPC. Estas pruebas no ejecutan el guardado completo de un personaje del juego.
- Helper de preparación: aplicación de ambas variantes, modo de comprobación, repetición sin cambios, firma/aplicación parcial, rutas con espacios y errores de Git/core. Parche específico comprobado sobre copias de los ObjectMgr completos del ZIP.
- Actualización de un core con el parche runtime anterior: el helper añade la protección de guardado faltante y comprueba todos los parches antes de aplicar alguno. Sus guardas extraídas del parche se compilan y ejecutan con dobles de oro/inventario y rollback.
- Recuperación: se compila el cuerpo real de `RequireRecovery` con dobles de Player/sesión. La regresión reproduce el fallo de oro sin objeto cuando no se activa la protección, y comprueba rollback, confirmación perdida y bloqueo antes de cerrar. Estas pruebas no ejecutan el proceso de logout del servidor completo.
- Recopilador: exclusión de configuración/enlaces y protección de archivos existentes.
- Aislamiento: no hay directorios de importación SQL ni consultas raw del módulo a los pools CharacterDatabase/WorldDatabase. Los únicos cambios de personaje se realizan mediante las API habituales del core.

Las seis unidades C++ del módulo, `ObjectMgr.cpp` y `PlayerStorage.cpp` del fork parcheado compilaron a archivos objeto sin diagnósticos con GCC20 y las dependencias de apoyo indicadas. No se enlazó ni ejecutó `worldserver`. La rama `_WIN32` de los helpers se comprueba mediante el preprocesador; esto no equivale a compilar con MSVC.

## Pendiente en el servidor de destino

- Compilar y enlazar el servidor completo en Visual Studio con sus dependencias exactas.
- Probar la conexión automática y sus permisos con la cuenta MySQL real.
- Validar en WoW 3.3.5a la inscripción, reconexión como druida, recursos, habilidades, talentos/glifos de ambas especializaciones y restauración al 80 para cada clase.
- Comprobar en el cliente real el addon actualizado, sus cartas, clics y solicitudes; reemplazarlo junto al módulo.
- Interrumpir conexiones/proceso durante guardados reales y comprobar recuperación de personajes y journal.
- Validar requisitos de armas, formas, posturas, mascotas y la pool; medir latencia de las transacciones síncronas bajo carga.

No se certifica el funcionamiento integral en WoW. Es una versión de desarrollo con verificaciones de código y componentes, pendiente de las pruebas del servidor y cliente reales.
