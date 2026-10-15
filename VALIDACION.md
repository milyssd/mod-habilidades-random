# Validación — Hollowsource y recuperación del personaje

Referencia: AzerothCore `9eabf314a27b71d4e9cd513f6f2c08befe90d4ef` con el parche incluido.

Destino: archivos `src/server` del `server.zip` enviado por el usuario, atribuidos por él a `yafeth950-test/hollowsource` `3927b32566d2910b332decb4fa97e3a2e0879b40`. Se inspeccionaron esas fuentes; el ZIP no permite verificar el hash Git. No contiene `src/common` ni `deps`: se usan los del core de referencia como apoyo a las comprobaciones C++.

Compatibilidad corregida después de la captura C3668:

- Se reprodujeron exactamente los cinco errores retirando esos cinco métodos de los encabezados de referencia, sin cambiar las demás API. Es una simulación de la superficie de hooks ausentes, no un checkout del core del usuario.
- Se comprueba el módulo completo contra los encabezados normales y contra esa superficie reducida.
- Un test C++17 ejecuta el despacho virtual real del adaptador para una API anterior, una API reciente y una firma diferente. Verifica que las llamadas llegan al bloqueo correcto.
- Se contrastaron además todos los overrides y las API de Player, mapas y base de datos con los archivos reales recibidos. El fork usa la ruta histórica `OnPlayerLearnSpell`.

Comprobaciones realizadas:

- Unidades C++ del módulo: `g++ -std=gnu++20 -fsyntax-only -Wall -Wextra -Werror=return-type` con encabezados reales del core y dependencias.
- Las cinco unidades C++ del módulo y `ObjectMgr.cpp` del ZIP con el parche específico se compilaron a archivos objeto mediante `g++ -std=gnu++20 -c -Wall -Wextra -Werror=return-type`. Se usaron sus encabezados reales, sin sustituir encabezados del servidor por los de referencia. Esto no enlaza ni ejecuta `worldserver`.
- El parche específico se aplica a una copia intacta del ZIP y admite comprobación inversa. No se cambió el contenido original subido. La prueba de sus dos métodos con fixtures mínimos y ASan/UBSan no encontró errores ni fugas; es una comprobación aislada, no ejecución del core.
- Preparación del core: 11 pruebas del helper con Git real, incluyendo ambas variantes, repetición sin cambios, modo `--check`, aplicación parcial, firma incompatible, rutas con espacios, ausencia de Git y fuentes incompatibles. También se aplicó y repitió sobre los dos archivos completos del ZIP. Una prueba adicional verifica que el recopilador de API no incluye configuración ni sobrescribe archivos.
- Test compilado de reglas: ofertas ajenas, saldos, bans/rerolls sin reemplazos y límites de participación/restauración.
- Test compilado del esquema embebido: diez tablas, versión 2 y 474 IDs únicos. Se comprueba el rechazo de nombres de bases del core/sistema e identificadores inválidos.
- Lua 5.1.5: sintaxis y ejecución del addon real con dobles de la API WoW. Lista de bloqueos vacía, inscripción después de estado inactivo, recepción de cartas, selección, bloqueo, ocultar/abrir, remitentes ajenos y callbacks pendientes tras cerrar.
- MariaDB 11.8.6, base desechable: se ejecutaron los DDL extraídos del encabezado C++ real; se comprobaron creación, primer sembrado, repetición sin reemplazar la pool, copias de ambas especializaciones y glifos, y rollback de un guardado fallido.
- Destrezas: migración aditiva desde v1 sin reemplazar copias ni pool; valores, paso y máximo persistentes; copia vacía diferenciada de copia antigua; detección de filas ausentes y rollback conjunto de inscripción/hechizos/destrezas. Esto comprueba persistencia y no sustituye la reconexión de un personaje en el juego.
- Se comprobaron en SQL las marcas persistentes de reconciliación: interrupción después del commit del evento, finalización pendiente con nivel 80, conservación de la copia y acuse final. Esto verifica el esquema/journal, no sustituye una prueba real del guardado de Player.
- Posiciones NPC: persistencia en la base privada y conservación al repetir el arranque.
- Comprobación de aislamiento: no quedan importaciones SQL para bases del core ni llamadas raw Query/Execute a CharacterDatabase/WorldDatabase en el módulo. El guardado del personaje usa las API y sentencias preparadas existentes de AzerothCore.

Conexión automática: si `DatabaseInfo` está vacío, se copia la configuración activa de `CharacterDatabase` y se cambia solamente la base de destino a `DatabaseName` (predeterminada: `mod_habilidades_random`). No se reconstruye ni registra una cadena con credenciales. La conexión del core y sus bases no se modifican. El modo explícito anterior sigue disponible como opción.

El test C++ de conexión comprueba copia independiente, autenticación, SSL, contraseña vacía, puertos, named pipe y el socket Unix que el core normaliza al abrir. Pasó en la rama nativa y con `_WIN32` definido para comprobar esa selección condicional; esto último no es una compilación MSVC. La conexión propia conserva su host de transporte al reabrirse, sin modificar el pool del core.

Cambios contrastados con el flujo real de Player en el fork: las destrezas se restauran después de reconstruir los hechizos, y `SendInitialSpells` sincroniza el libro al terminar. El cambio de clase actualiza también CharacterCache conservando hermandad, arena, correo y grupo. Estas rutas requieren comprobación final dentro del juego.

Pendiente antes de producción:

- Compilar y enlazar el servidor completo con MSVC y las dependencias exactas del destino. No se ejecutó Visual Studio en este entorno.
- Probar apertura automática de la conexión y permisos con la cuenta MySQL real del core; debe poder crear y administrar la base del evento.
- Probar en el juego `.hr npc`, interacción, persistencia del NPC tras reiniciar, conversión sin cambiar de raza y restauración de cada clase al 80, incluyendo destrezas tras reconectar y actualización del libro sin reconectar.
- Interrumpir de verdad el proceso o una conexión entre el commit privado y el guardado del core; comprobar la reconciliación de personajes, talentos y glifos.
- Validar poderes, requisitos de armas/formas/mascotas y rendimiento bajo carga.

No se certifica todavía el funcionamiento integral dentro de WoW. Es una versión de desarrollo con verificaciones parciales y requisitos explícitos de integración.
