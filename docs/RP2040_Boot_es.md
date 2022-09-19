# Arrancando desde un RP2040 Pico
Esta guía describe cómo instalar cubeboot como un firmware integrado para el pico. Esto incluye los dispositivos PicoBoot instalados en el IPL.

## Instalación

Descarga el archivo de firmware `cubeboot.uf2` más reciente de la página releases de GitHub. Opcionalmente, desuelda el cable VCC de tu pico para evitar un sobrevoltaje en tu GameCube mientras lo flasheas (puedes omitir este paso si tienes un diodo instalado en VCC).

Mantén presionado BOOTSEL mientras conectas tu pico a tu computadora. Esto hace que aparezca como una unidad de disco en tu computadora. Copia `cubeboot.uf2` en la unidad de disco y espera hasta que desaparezca. El firmware se ha actualizado con éxito.

Si desoldaste VCC, debes volver a soldarlo ahora antes de reiniciar tu GameCube de nuevo.

Asegúrate de descargar una copia de `cubeboot.ini` y cópiala en tu tarjeta SD. Este archivo de configuración te permite personalizar los aspectos del proceso de arranque.

Ya no necesitas un archivo IPL.dol en tu tarjeta SD después de instalar cubeboot como firmware.

## Problemas

Algunas tarjetas SD no funcionan bien con cubeboot y harán que se reproduzca la animación de GameCube antes de abrir el menú. Si tu GameCube arranca en el menú y dice "NO DISC", entonces debes seguir los siguientes pasos.

Para solucionar el problema de "NO DISC", descarga el archivo fallback.bin más reciente de la página de releases de GitHub y cópialo a tu tarjeta SD. Entonces abre el archivo `cubeboot.ini` y agrega la línea `force_fallback = 1`.

Una vez que hagas esto, ya deberías poder ejecutar cubeboot sin quedarte atascado en el menú de GameCube.
