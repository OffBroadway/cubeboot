# Arrancando desde una SD

Esta guía describe cómo puedes usar cubeboot en un sistema con iplboot instalado como programa inicial. Esto incluye dispositivos PicoBoot con firmware de fábrica, así como dispositivos Viper y Qoob con iplboot instalado en su memoria flash interna.

## Instalación

Para instalar y usar con iplboot, simplemente cambia el nombre de tu IPL.dol actual a boot.dol. Descarga el archivo `cubeboot.dol` más reciente de la página releases de GitHub. Ahora cambia el nombre del archivo `cubeboot.dol` que descargaste a IPL.dol y cópialo a la tarjeta SD.

También debes descargar el `cubeboot.ini` incluido y cambiar cualquier configuración que desees antes de continuar. Este archivo de configuración te permite personalizar aspectos del proceso de arranque.

Una vez que hayas confirmado que la tarjeta SD contiene IPL.dol, boot.dol y `cubeboot.ini`, ¡ya estás listo para comenzar!

## Problemas

Algunas tarjetas SD no funcionan bien con cubeboot y harán que se reproduzca la animación de GameCube antes de abrir el menú. Si tu GameCube arranca en el menú y dice "NO DISC", entonces debes seguir los siguientes pasos.

Para solucionar el problema de "NO DISC", descarga el archivo fallback.bin más reciente de la página de releases de GitHub y cópialo a tu tarjeta SD. Entonces abre el archivo `cubeboot.ini` y agrega la línea `force_fallback = 1`.

Una vez que hagas esto, ya deberías poder ejecutar cubeboot sin quedarte atascado en el menú de GameCube.
