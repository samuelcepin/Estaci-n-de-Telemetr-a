"""Pruebas locales; no necesitan credenciales ni acceso a la red."""

from __future__ import annotations

import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ProyectoLocalTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        env = os.environ.copy()
        env.setdefault("LC_ALL", "C")
        clean = subprocess.run(
            ["make", "clean"], cwd=ROOT, env=env,
            text=True, capture_output=True, check=False,
        )
        if clean.returncode != 0:
            raise RuntimeError(f"make clean fallo:\n{clean.stdout}\n{clean.stderr}")
        build = subprocess.run(
            ["make"], cwd=ROOT, env=env,
            text=True, capture_output=True, check=False,
        )
        output = build.stdout + build.stderr
        if build.returncode != 0:
            raise RuntimeError(f"make fallo:\n{output}")
        if re.search(r"\bwarning:\s", output, flags=re.IGNORECASE):
            raise RuntimeError(f"La compilacion produjo advertencias:\n{output}")
        cls.build_output = output

        cls.temp_dir = tempfile.TemporaryDirectory(prefix="itla_sqlite_test_")
        cls.sqlite_smoke = Path(cls.temp_dir.name) / "sqlite_smoke"
        compile_smoke = subprocess.run(
            [
                "gcc", "-std=c11", "-Wall", "-Wextra", "-O2",
                "-Isrc", "-Ilib/sqlite",
                "tests/sqlite_smoke.c", "src/datos.c", "build/sqlite3.o",
                "-lm", "-ldl", "-lpthread", "-o", str(cls.sqlite_smoke),
            ],
            cwd=ROOT, env=env, text=True, capture_output=True, check=False,
        )
        if compile_smoke.returncode != 0:
            raise RuntimeError(
                "No se pudo compilar la prueba SQLite:\n"
                + compile_smoke.stdout + compile_smoke.stderr
            )

        cls.sensor_smoke = Path(cls.temp_dir.name) / "sensor_smoke"
        compile_sensor = subprocess.run(
            [
                "gcc", "-std=c11", "-Wall", "-Wextra", "-O2",
                "-Isrc", "-Ilib/sqlite",
                "tests/sensor_smoke.c", "src/sensor.c", "-lm",
                "-o", str(cls.sensor_smoke),
            ],
            cwd=ROOT, env=env, text=True, capture_output=True, check=False,
        )
        if compile_sensor.returncode != 0:
            raise RuntimeError(
                "No se pudo compilar la prueba del sensor:\n"
                + compile_sensor.stdout + compile_sensor.stderr
            )

    @classmethod
    def tearDownClass(cls) -> None:
        if hasattr(cls, "temp_dir"):
            cls.temp_dir.cleanup()

    def test_estructura_minima_del_entregable(self) -> None:
        required = [
            "src/main.c", "src/red.c", "src/red.h", "src/datos.c",
            "src/datos.h", "src/sensor.c", "src/sensor.h",
            "lib/sqlite/sqlite3.c", "lib/sqlite/sqlite3.h",
            "Makefile", "README.md",
        ]
        for relative in required:
            with self.subTest(archivo=relative):
                self.assertTrue((ROOT / relative).is_file(), relative)

    def test_makefile_compila_sqlite_amalgamation(self) -> None:
        makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        self.assertIn("SQLITE_DIR = lib/sqlite", makefile)
        self.assertIn("SQLITE_C = $(SQLITE_DIR)/sqlite3.c", makefile)
        self.assertIn("SQLITE_OBJ = $(BUILD_DIR)/sqlite3.o", makefile)
        self.assertIn("-std=c11", makefile)
        self.assertIn("-Wall", makefile)
        self.assertIn("-Wextra", makefile)

    def test_codigo_no_contiene_token_de_32_caracteres(self) -> None:
        archivos = [
            ROOT / "src/main.c", ROOT / "src/red.c", ROOT / "src/datos.c",
            ROOT / "src/sensor.c", ROOT / "README.md", ROOT / "Makefile",
        ]
        patron = re.compile(r"(?<![A-Fa-f0-9])[A-Fa-f0-9]{32}(?![A-Fa-f0-9])")
        for archivo in archivos:
            with self.subTest(archivo=archivo.name):
                contenido = archivo.read_text(encoding="utf-8")
                self.assertIsNone(patron.search(contenido),
                                  f"Posible secreto incrustado en {archivo.name}")

    def test_compilacion_sin_advertencias(self) -> None:
        self.assertTrue((ROOT / "estacion").is_file() or (ROOT / "estacion.exe").is_file())
        self.assertTrue((ROOT / "build/sqlite3.o").is_file())
        self.assertNotRegex(self.build_output, r"(?i)\bwarning:\s")

    def test_ejecutable_muestra_uso_sin_argumentos(self) -> None:
        result = subprocess.run(
            [str(ROOT / "estacion")], cwd=ROOT,
            text=True, capture_output=True, check=False,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Uso:", result.stderr)

    def test_sqlite_integracion_wal_sql_y_colas(self) -> None:
        db_path = Path(self.temp_dir.name) / "estacion_prueba.db"
        result = subprocess.run(
            [str(self.sqlite_smoke), str(db_path)], cwd=ROOT,
            text=True, capture_output=True, check=False,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("prueba SQLite superada", result.stdout)
        self.assertTrue(db_path.is_file())

    def test_sensor_simulado_respeta_rangos_del_enunciado(self) -> None:
        result = subprocess.run(
            [str(self.sensor_smoke)], cwd=ROOT,
            text=True, capture_output=True, check=False,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("sensor simulado dentro de rango", result.stdout)


if __name__ == "__main__":
    unittest.main(verbosity=2)