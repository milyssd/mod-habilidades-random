# Validación — base privada y registro desde código

Referencia: AzerothCore `9eabf314a27b71d4e9cd513f6f2c08befe90d4ef` con el parche incluido.

Comprobaciones realizadas:

- Unidades C++ del módulo: `g++ -std=gnu++20 -fsyntax-only -Wall -Wextra -Werror=return-type` con encabezados reales del core y dependencias.
- Archivo `ObjectMgr.cpp` con las dos API nuevas: misma comprobación de C++ sobre el archivo real. El linter C++ de AzerothCore pasó. No se configuró, compiló ni enlazó el core completo.
- Test compilado de reglas: ofertas ajenas, saldos, bans/rerolls sin reemplazos y límites de participación/restauración.
- Test compilado del esquema embebido: ocho tablas y 474 IDs únicos. Se comprueba el rechazo de nombres de bases del core/sistema e identificadores inválidos.
- Lua 5.1.5: sintaxis y ejecución del addon real con dobles de la API WoW. Lista de bloqueos vacía, inscripción después de estado inactivo, recepción de cartas, selección, bloqueo, ocultar/abrir, remitentes ajenos y callbacks pendientes tras cerrar.
- MariaDB 11.8.6, base desechable: se ejecutaron los DDL extraídos del encabezado C++ real; se comprobaron creación, primer sembrado, repetición sin reemplazar la pool, copias de ambas especializaciones y glifos, y rollback de un guardado fallido.
- Se comprobaron en SQL las marcas persistentes de reconciliación: interrupción después del commit del evento, finalización pendiente con nivel 80, conservación de la copia y acuse final. Esto verifica el esquema/journal, no sustituye una prueba real del guardado de Player.
- Posiciones NPC: persistencia en la base privada y conservación al repetir el arranque.
- Comprobación de aislamiento: no quedan importaciones SQL para bases del core ni llamadas raw Query/Execute a CharacterDatabase/WorldDatabase en el módulo. El guardado del personaje usa las API y sentencias preparadas existentes de AzerothCore.

Pendiente antes de producción:

- Compilar y enlazar el servidor completo en el commit exacto del destino.
- Probar apertura automática de la conexión y permisos con el usuario MySQL real del evento.
- Probar en el juego `.hr npc`, interacción, persistencia del NPC tras reiniciar, conversión sin cambiar de raza y restauración de cada clase al 80.
- Interrumpir de verdad el proceso o una conexión entre el commit privado y el guardado del core; comprobar la reconciliación de personajes, talentos y glifos.
- Validar poderes, requisitos de armas/formas/mascotas y rendimiento bajo carga.

No se certifica todavía el funcionamiento integral dentro de WoW. Es una versión de desarrollo con verificaciones parciales y requisitos explícitos de integración.
