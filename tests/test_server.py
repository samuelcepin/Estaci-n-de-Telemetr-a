"""Pruebas de solo lectura del servidor de curso ITLA.

No hace POST, no publica MQTT y no altera las metricas del estudiante.
Requiere ITLA_HOST; config/progreso tambien requieren ITLA_MATRICULA e ITLA_TOKEN.
"""

from __future__ import annotations

import os
import socket
import unittest
from urllib.error import HTTPError, URLError
from urllib.parse import urlsplit
from urllib.request import Request, urlopen


def _host_and_api_base() -> tuple[str, str]:
    raw = os.environ.get("ITLA_API_BASE", "").strip() or os.environ.get("ITLA_HOST", "").strip()
    if not raw:
        raise RuntimeError("Define ITLA_HOST o ITLA_API_BASE antes de ejecutar estas pruebas.")

    if "://" not in raw:
        raw = "http://" + raw
    parsed = urlsplit(raw)
    host = parsed.hostname
    if not host:
        raise RuntimeError("ITLA_HOST no contiene un nombre de host valido.")

    port = parsed.port or 4000
    scheme = parsed.scheme or "http"
    base = f"{scheme}://{host}:{port}"
    return host, base.rstrip("/")


class PruebasServidorSoloLectura(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        if not os.environ.get("ITLA_API_BASE", "").strip() and not os.environ.get("ITLA_HOST", "").strip():
            raise unittest.SkipTest("Define ITLA_HOST o ITLA_API_BASE para ejecutar pruebas del servidor.")
        cls.host, cls.base = _host_and_api_base()
        cls.token = os.environ.get("ITLA_TOKEN", "").strip()
        cls.matricula = os.environ.get("ITLA_MATRICULA", "").strip()
        cls.timeout = float(os.environ.get("ITLA_TEST_TIMEOUT", "8"))
        cls.mqtt_port = int(os.environ.get("ITLA_MQTT_PORT", "1883"))

    def get(self, path: str, authenticated: bool = False) -> tuple[int, str, str]:
        headers = {"User-Agent": "estacion-itla-tests/1.0"}
        if authenticated:
            if not self.token:
                self.skipTest("Falta ITLA_TOKEN para la prueba autenticada.")
            headers["Authorization"] = f"Bearer {self.token}"
        request = Request(self.base + path, headers=headers, method="GET")
        try:
            with urlopen(request, timeout=self.timeout) as response:
                body = response.read(1024 * 1024).decode("utf-8", errors="replace")
                content_type = response.headers.get("Content-Type", "")
                return response.status, body, content_type
        except HTTPError as exc:
            body = exc.read(1024 * 1024).decode("utf-8", errors="replace")
            return exc.code, body, exc.headers.get("Content-Type", "")
        except URLError as exc:
            self.fail(f"No se pudo conectar con {self.base}{path}: {exc.reason}")

    def test_01_dns_resuelve_host_del_servidor(self) -> None:
        addresses = socket.getaddrinfo(self.host, 4000, type=socket.SOCK_STREAM)
        self.assertTrue(addresses, "El host no devolvio direcciones IP.")

    def test_02_api_ping_publico(self) -> None:
        status, body, _ = self.get("/api/v1/ping")
        self.assertEqual(status, 200, body)
        self.assertIn("ok=1", body)

    def test_03_tablero_publico(self) -> None:
        status, body, content_type = self.get("/tablero")
        self.assertEqual(status, 200, body[:500])
        self.assertTrue("text/html" in content_type.lower() or "<html" in body.lower(),
                        "El endpoint /tablero no parece devolver HTML.")

    def test_04_configuracion_autenticada(self) -> None:
        if not self.matricula:
            self.skipTest("Falta ITLA_MATRICULA para comprobar la configuracion individual.")
        status, body, _ = self.get("/api/v1/config", authenticated=True)
        self.assertEqual(status, 200, body)
        values = {}
        for line in body.splitlines():
            if "=" in line:
                key, value = line.split("=", 1)
                values[key.strip()] = value.strip()
        required = {"matricula", "hora", "umbral_temp", "intervalo_s", "latido_s", "maquinas", "lote_max"}
        self.assertTrue(required.issubset(values),
                        f"Faltan claves de configuracion: {sorted(required - values.keys())}")
        self.assertEqual(values["matricula"], self.matricula,
                         "El servidor devolvio una matricula distinta a ITLA_MATRICULA.")

    def test_05_progreso_autenticado(self) -> None:
        if not self.matricula:
            self.skipTest("Falta ITLA_MATRICULA para comprobar el progreso individual.")
        status, body, _ = self.get("/api/v1/mi-progreso", authenticated=True)
        self.assertEqual(status, 200, body)
        self.assertIn("nivel0", body)
        self.assertIn("nivel5", body)

    def test_06_lecturas_csv_autenticadas(self) -> None:
        if not self.matricula:
            self.skipTest("Falta ITLA_MATRICULA para comprobar lecturas.")
        status, body, _ = self.get(
            "/api/v1/lecturas?desde_id=0&limite=1", authenticated=True
        )
        self.assertEqual(status, 200, body)
        lines = [line.strip() for line in body.splitlines() if line.strip()]
        self.assertGreaterEqual(len(lines), 1, "La respuesta CSV esta vacia.")
        expected = "id,maquina,ts,temp,corriente,vibracion,estado"
        self.assertEqual(lines[0].replace("\r", ""), expected,
                         "La cabecera CSV no coincide con la especificacion.")
        self.assertLessEqual(len(lines) - 1, 1,
                             "El servidor devolvio mas de una fila para limite=1.")

    def test_07_puerto_mqtt_accesible(self) -> None:
        try:
            with socket.create_connection((self.host, self.mqtt_port), timeout=self.timeout):
                pass
        except OSError as exc:
            self.fail(f"No se pudo abrir TCP hacia MQTT {self.host}:{self.mqtt_port}: {exc}")


if __name__ == "__main__":
    unittest.main(verbosity=2)
