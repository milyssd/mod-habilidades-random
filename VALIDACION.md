# Validación de esta entrega

Referencia de API: AzerothCore `9eabf314a27b71d4e9cd513f6f2c08befe90d4ef`.

Comprobaciones completadas:

- Todas las unidades C++ del módulo pasan `g++ -std=gnu++20 -fsyntax-only -Wall -Wextra -Werror=return-type` usando los encabezados reales de ese commit, Boost 1.83 y las dependencias de encabezados correspondientes. Se verifican los overrides y las llamadas a las API. No se configuró ni compiló el core completo.
- Compilación y ejecución del test de reglas de estado: rechaza elecciones ajenas, bans y rerolls sin sustitutos o saldo; comprueba el límite 79/80/81, la participación explícita y los bloqueos durante la restauración.
- El addon pasa el análisis de sintaxis con **Lua 5.1.5**. El test ejecuta su código real con dobles de la API de WoW: recepción de lista de bans vacía, inscripción después de estado inactivo, cartas, ban, selección, ocultar/abrir, rechazo de remitente ajeno y cierre definitivo frente a callbacks pendientes.
- Integración SQL con **MariaDB 11.8.6**, en una base desechable. Se comprobaron los 474 IDs únicos de la pool, las copias de ambas especializaciones y glifos, la conservación de copias al repetir el esquema y el rollback de un guardado fallido.
- El SQL de mundo se probó contra los esquemas reales del core. Añade las diez combinaciones raza/druida, conserva las combinaciones y coordenadas previas, se puede repetir y no sobrescribe un NPC ajeno con entrada coincidente.

Pendiente antes de producción:

- Enlazar/compilar `worldserver` en la versión exacta del servidor de destino.
- Ensayo real con cliente 3.3.5a, datos DBC/mapas y bases de AzerothCore: NPC → copia → conversión manteniendo raza → combate y elecciones → reconexión → nivel 80 → restauración. Incluir cada clase original, ambas especializaciones, glifos y personajes con mascota.
- Validación funcional de las habilidades elegibles, sus dependencias, posturas, formas, armas y mascotas. El filtro de recursos no sustituye esas pruebas.
- Medir el coste de los guardados confirmados con la carga esperada del evento.

Estos resultados no certifican todavía que el desafío completo funcione dentro de WoW. La entrega es una primera implementación corregida y comprobada para continuar las pruebas de integración.
