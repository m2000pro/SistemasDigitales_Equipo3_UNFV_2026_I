<h2>Descripción del codigo fuente</h2>
*El archivo "main.cpp" contiene el firmware almacenado en la memoria Flash del microcontrolador:
<br>
-Inicialización: Variables de contexto, variables de telemetría (monitoreo), definición de estados, declaraciones de funciones.<br>
-Funciones auxiliares: Gestión de conexión y monitoreo.<br>
-Bucle de inicio: Encendido y comprobación inicial. Carga de variables de configuración (ej.: Credenciales wifi). <br><br>
*El archivo "librerias.ini" contiene todas las librerías no nativas instaladas.<br>
*El archivo "secrets.h" contiene los URLs a los endpoints de validación, auditoría y reservas extraordinarias, así como una configuración por defecto para WIFI y clave maestra de red.
