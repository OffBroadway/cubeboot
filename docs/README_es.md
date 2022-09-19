Flippyboot IPL
Este proyecto es un framework de parcheo para el GameCube IPL, llamado BS2. El proyecto originalmente estaba destinado a restaurar la animación de arranque en Flippyboot. Ahora el proyecto se ha generalizado y funciona tanto en Flippyboot como en PicoBoot.
cubeboot actúa como un arnés de parcheo para BS2 y es capaz de montar dispositivos FAT externos y cargar una cadena DOL. El BS1 y Font ROM con derechos de autor no se proporcionan ni son necesarios con Flippyboot, ya que estos residen en la U10 ROM del GameCube.
cubeboot puede inyectar el BS2 en una imagen ROM codificada existente con fines de simulación a través de "make dolphinipl.bin" en el directorio ipl. Debes proporcionar la imagen ROM original para la inyección. De nuevo, esto sólo es necesario para desarrollo y depuración.
Uso
Si estás usando cubeboot con un reemplazo de IPL que se carga con iplboot (como PicoBoot), puedes instalar cubeboot en una tarjeta SD siguiendo el tutorial de Arranque con USB (SD Booting).
cubeboot también incluye un modo alternativo en el que puede cargar tu archivo DOL antes de la animación de arranque.
Compilado
Este proyecto contiene todos los scripts para compilar el cubeboot usando los devkitPPC y GCC más recientes. Además, se proporcionan scripts que codifican la imagen BS2 de forma adecuada para su inyección sobre el BS2 de fábrica en el GCN.
Características
• ☒ Restaura la animación de arranque
- ☐ con características de iplboot
• ☒ Carga un IPL alternativo desde una tarjeta SD
• ☒ Admite todas las revisiones IPL de NTSC y PAL
• ☒ Admite el arranque con SDGecko A/B y SD2SP2
• ☐ Imagen de firmware flashable para picoboot (gzip)
• ☒ Carga la configuración desde una tarjeta SD
• ☒ Colores de animación de GameCube personalizados (probado)
- ☒ Color aleatorio para cada arranque usando RTC
• ☐ Reemplazo de texto personalizado del logotipo de Nintendo 
• ☐ Fuerza los modos de video progresivo
Compatibilidad
Versiones IPL compatibles conocidas: - NTSC 1.0 - NTSC 1.1 (sim + hardware verificado) - NTSC 1.2 (DOL-001 y DOL-101) - PAL 1.0 - MPAL 1.1 - PAL 1.2
Pendientes
• ☐ Crear acciones de GitHub para CI/CD
• ☐ Agregar soporte para GCLoader
• ☐ Archivos de configuración flashables para picoboot
• ☐ Agregar soporte para tarjeta de memoria boot.dol y ipl.bin
Pendientes de Prelanzamiento
• ☐ Escribir documentación del tutorial (traducción al español por Fiverr)
• ☐ Agregar valor de configuración para cargar el respaldo (fallback)
• ☐ Agregar configuración para 480p forzado
• ☐ Agregar configuración para cube_text
