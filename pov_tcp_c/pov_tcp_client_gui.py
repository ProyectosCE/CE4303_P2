#!/usr/bin/env python3
"""
GUI Tkinter para ejecutar pov_tcp_client.

Esta interfaz NO implementa la lógica del cliente TCP.
Solo construye y ejecuta el binario C `pov_tcp_client` con los parámetros elegidos.

Requisitos:
  - Python 3 con tkinter.
  - Binario C compilado: ./pov_tcp_client

Uso:
  python3 pov_tcp_client_gui.py
"""

import os
import queue
import subprocess
import threading
import tkinter as tk
from pathlib import Path
from tkinter import filedialog, messagebox, ttk


PATTERNS = ["circle", "checker", "diagonal", "waves"]


class PovTcpClientGui:
    def __init__(self, root):
        self.root = root
        self.root.title("POV TCP Client GUI")
        self.root.geometry("1080x760")
        self.root.minsize(980, 680)

        self.process = None
        self.worker_thread = None
        self.output_queue = queue.Queue()

        self._build_variables()
        self._build_ui()
        self._set_running(False)
        self.root.after(100, self._poll_output_queue)

    # ============================================================
    # Variables
    # ============================================================

    def _build_variables(self):
        self.client_path = tk.StringVar(value="./pov_tcp_client")
        self.working_dir = tk.StringVar(value="")

        self.server_ip = tk.StringVar(value="127.0.0.1")
        self.port = tk.StringVar(value="5050")

        self.width = tk.StringVar(value="1024")
        self.height = tk.StringVar(value="1024")
        self.pattern = tk.StringVar(value="waves")
        self.rounds = tk.StringVar(value="128")
        self.speed_ms = tk.StringVar(value="4")

        self.output_bin = tk.StringVar(value="payload_1024.bin")
        self.output_pbm = tk.StringVar(value="grande_1024.pbm")
        self.results_dir = tk.StringVar(value="client_results")

        self.enable_output_bin = tk.BooleanVar(value=True)
        self.enable_output_pbm = tk.BooleanVar(value=True)
        self.save_pbm = tk.BooleanVar(value=True)
        self.return_pbm = tk.BooleanVar(value=True)
        self.return_packet = tk.BooleanVar(value=True)
        self.no_run = tk.BooleanVar(value=False)

        self.custom_extra_args = tk.StringVar(value="")

    # ============================================================
    # UI
    # ============================================================

    def _build_ui(self):
        outer = ttk.Frame(self.root, padding=10)
        outer.pack(fill=tk.BOTH, expand=True)

        self.notebook = ttk.Notebook(outer)
        self.notebook.pack(fill=tk.BOTH, expand=True)

        self.config_frame = ttk.Frame(self.notebook, padding=10)
        self.log_frame = ttk.Frame(self.notebook, padding=10)

        self.notebook.add(self.config_frame, text="Configuración")
        self.notebook.add(self.log_frame, text="Salida / Logs")

        self._build_config_tab()
        self._build_log_tab()
        self._build_bottom_bar(outer)

        self._wire_state_callbacks()
        self._update_option_states()
        self._update_command_preview()

    def _build_config_tab(self):
        canvas = tk.Canvas(self.config_frame, highlightthickness=0)
        scrollbar = ttk.Scrollbar(self.config_frame, orient=tk.VERTICAL, command=canvas.yview)
        content = ttk.Frame(canvas)

        content.bind(
            "<Configure>",
            lambda event: canvas.configure(scrollregion=canvas.bbox("all")),
        )

        canvas.create_window((0, 0), window=content, anchor="nw")
        canvas.configure(yscrollcommand=scrollbar.set)

        canvas.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        scrollbar.pack(side=tk.RIGHT, fill=tk.Y)

        self._add_executable_section(content)
        self._add_network_section(content)
        self._add_generation_section(content)
        self._add_output_section(content)
        self._add_behavior_section(content)
        self._add_command_preview(content)

    def _section(self, parent, title):
        frame = ttk.LabelFrame(parent, text=title, padding=10)
        frame.pack(fill=tk.X, expand=True, pady=(0, 10))
        frame.columnconfigure(1, weight=1)
        return frame

    def _entry_row(self, parent, row, label, variable, browse=None):
        ttk.Label(parent, text=label).grid(row=row, column=0, sticky="w", padx=(0, 8), pady=4)
        entry = ttk.Entry(parent, textvariable=variable)
        entry.grid(row=row, column=1, sticky="ew", pady=4)

        if browse is not None:
            button = ttk.Button(parent, text="Buscar...", command=browse)
            button.grid(row=row, column=2, sticky="e", padx=(8, 0), pady=4)
        return entry

    def _add_executable_section(self, parent):
        frame = self._section(parent, "Cliente C")

        self._entry_row(
            frame,
            0,
            "Ejecutable pov_tcp_client:",
            self.client_path,
            self._browse_client,
        )

        self._entry_row(
            frame,
            1,
            "Directorio de trabajo:",
            self.working_dir,
            self._browse_working_dir,
        )

        ttk.Label(
            frame,
            text=(
                "Si el directorio de trabajo queda vacío, se usa el directorio actual "
                "desde donde se abrió esta GUI."
            ),
            foreground="#555555",
        ).grid(row=2, column=0, columnspan=3, sticky="w", pady=(4, 0))

    def _add_network_section(self, parent):
        frame = self._section(parent, "Servidor TCP")
        self._entry_row(frame, 0, "Server IP:", self.server_ip)
        self._entry_row(frame, 1, "Puerto:", self.port)

    def _add_generation_section(self, parent):
        frame = self._section(parent, "Generación PBM + cifrado XTEA")

        self._entry_row(frame, 0, "Width:", self.width)
        self._entry_row(frame, 1, "Height:", self.height)

        ttk.Label(frame, text="Pattern:").grid(row=2, column=0, sticky="w", padx=(0, 8), pady=4)
        pattern_box = ttk.Combobox(
            frame,
            textvariable=self.pattern,
            values=PATTERNS,
            state="readonly",
        )
        pattern_box.grid(row=2, column=1, sticky="ew", pady=4)

        self._entry_row(frame, 3, "Rounds XTEA:", self.rounds)
        self._entry_row(frame, 4, "Speed ms:", self.speed_ms)

    def _add_output_section(self, parent):
        frame = self._section(parent, "Archivos locales")

        self.output_bin_check = ttk.Checkbutton(
            frame,
            text="Usar --output-bin",
            variable=self.enable_output_bin,
            command=self._update_option_states,
        )
        self.output_bin_check.grid(row=0, column=0, sticky="w", pady=4)

        self.output_bin_entry = ttk.Entry(frame, textvariable=self.output_bin)
        self.output_bin_entry.grid(row=0, column=1, sticky="ew", pady=4)
        ttk.Button(frame, text="Buscar...", command=self._browse_output_bin).grid(
            row=0, column=2, sticky="e", padx=(8, 0), pady=4
        )

        self.output_pbm_check = ttk.Checkbutton(
            frame,
            text="Usar --output-pbm",
            variable=self.enable_output_pbm,
            command=self._update_option_states,
        )
        self.output_pbm_check.grid(row=1, column=0, sticky="w", pady=4)

        self.output_pbm_entry = ttk.Entry(frame, textvariable=self.output_pbm)
        self.output_pbm_entry.grid(row=1, column=1, sticky="ew", pady=4)
        ttk.Button(frame, text="Buscar...", command=self._browse_output_pbm).grid(
            row=1, column=2, sticky="e", padx=(8, 0), pady=4
        )

        self.results_dir_entry = self._entry_row(
            frame,
            2,
            "Results dir:",
            self.results_dir,
            self._browse_results_dir,
        )

    def _add_behavior_section(self, parent):
        frame = self._section(parent, "Opciones de ejecución")

        ttk.Checkbutton(
            frame,
            text="Guardar PBM grande localmente",
            variable=self.save_pbm,
            command=self._update_option_states,
        ).grid(row=0, column=0, sticky="w", pady=4)

        ttk.Checkbutton(
            frame,
            text="Pedir PBM final al servidor (--return-pbm)",
            variable=self.return_pbm,
            command=self._update_option_states,
        ).grid(row=1, column=0, sticky="w", pady=4)

        ttk.Checkbutton(
            frame,
            text="Pedir driver_packet.bin al servidor (--return-packet)",
            variable=self.return_packet,
            command=self._update_option_states,
        ).grid(row=2, column=0, sticky="w", pady=4)

        ttk.Checkbutton(
            frame,
            text="Solo enviar/guardar, no ejecutar MPI en servidor (--no-run)",
            variable=self.no_run,
            command=self._update_option_states,
        ).grid(row=3, column=0, sticky="w", pady=4)

        ttk.Label(frame, text="Argumentos extra:").grid(row=4, column=0, sticky="w", pady=(10, 4))
        extra_entry = ttk.Entry(frame, textvariable=self.custom_extra_args)
        extra_entry.grid(row=5, column=0, columnspan=3, sticky="ew", pady=4)

        ttk.Label(
            frame,
            text="Usar solo para opciones adicionales soportadas por pov_tcp_client.",
            foreground="#555555",
        ).grid(row=6, column=0, columnspan=3, sticky="w")

    def _add_command_preview(self, parent):
        frame = self._section(parent, "Comando generado")
        self.command_preview = tk.Text(frame, height=5, wrap=tk.WORD)
        self.command_preview.pack(fill=tk.X, expand=True)
        self.command_preview.configure(state=tk.DISABLED)

    def _build_log_tab(self):
        self.log_text = tk.Text(self.log_frame, wrap=tk.WORD, font=("Consolas", 10))
        self.log_text.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)

        scrollbar = ttk.Scrollbar(self.log_frame, orient=tk.VERTICAL, command=self.log_text.yview)
        scrollbar.pack(side=tk.RIGHT, fill=tk.Y)
        self.log_text.configure(yscrollcommand=scrollbar.set)

    def _build_bottom_bar(self, parent):
        bar = ttk.Frame(parent)
        bar.pack(fill=tk.X, pady=(10, 0))
        bar.columnconfigure(1, weight=1)

        self.status_label = ttk.Label(bar, text="Listo.")
        self.status_label.grid(row=0, column=0, sticky="w")

        self.progress = ttk.Progressbar(bar, mode="indeterminate")
        self.progress.grid(row=0, column=1, sticky="ew", padx=12)

        self.clear_button = ttk.Button(bar, text="Limpiar log", command=self._clear_log)
        self.clear_button.grid(row=0, column=2, padx=(0, 6))

        self.run_button = ttk.Button(bar, text="Ejecutar", command=self._start_process)
        self.run_button.grid(row=0, column=3, padx=(0, 6))

        self.stop_button = ttk.Button(bar, text="Detener", command=self._stop_process)
        self.stop_button.grid(row=0, column=4)

    def _wire_state_callbacks(self):
        variables = [
            self.client_path,
            self.working_dir,
            self.server_ip,
            self.port,
            self.width,
            self.height,
            self.pattern,
            self.rounds,
            self.speed_ms,
            self.output_bin,
            self.output_pbm,
            self.results_dir,
            self.custom_extra_args,
            self.enable_output_bin,
            self.enable_output_pbm,
            self.save_pbm,
            self.return_pbm,
            self.return_packet,
            self.no_run,
        ]

        for variable in variables:
            variable.trace_add("write", lambda *_: self._update_command_preview())

    # ============================================================
    # Browse helpers
    # ============================================================

    def _browse_client(self):
        path = filedialog.askopenfilename(title="Seleccionar pov_tcp_client")
        if path:
            self.client_path.set(path)

    def _browse_working_dir(self):
        path = filedialog.askdirectory(title="Seleccionar directorio de trabajo")
        if path:
            self.working_dir.set(path)

    def _browse_output_bin(self):
        path = filedialog.asksaveasfilename(
            title="Guardar payload .bin",
            defaultextension=".bin",
            filetypes=[("BIN files", "*.bin"), ("All files", "*.*")],
        )
        if path:
            self.output_bin.set(path)

    def _browse_output_pbm(self):
        path = filedialog.asksaveasfilename(
            title="Guardar PBM grande",
            defaultextension=".pbm",
            filetypes=[("PBM files", "*.pbm"), ("All files", "*.*")],
        )
        if path:
            self.output_pbm.set(path)

    def _browse_results_dir(self):
        path = filedialog.askdirectory(title="Seleccionar directorio de resultados")
        if path:
            self.results_dir.set(path)

    # ============================================================
    # Command building
    # ============================================================

    def _validate_positive_int(self, name, value):
        try:
            parsed = int(value)
        except ValueError as exc:
            raise ValueError(f"{name} debe ser un entero.") from exc

        if parsed <= 0:
            raise ValueError(f"{name} debe ser mayor que cero.")

        return parsed

    def _build_command(self):
        client = self.client_path.get().strip()
        if not client:
            raise ValueError("Debe indicar la ruta del ejecutable pov_tcp_client.")

        server_ip = self.server_ip.get().strip()
        if not server_ip:
            raise ValueError("Debe indicar server IP.")

        port = self._validate_positive_int("Puerto", self.port.get())
        width = self._validate_positive_int("Width", self.width.get())
        height = self._validate_positive_int("Height", self.height.get())
        rounds = self._validate_positive_int("Rounds", self.rounds.get())
        speed_ms = self._validate_positive_int("Speed ms", self.speed_ms.get())

        command = [
            client,
            "--server-ip", server_ip,
            "--port", str(port),
            "--width", str(width),
            "--height", str(height),
            "--pattern", self.pattern.get(),
            "--rounds", str(rounds),
            "--speed-ms", str(speed_ms),
        ]

        if self.enable_output_bin.get():
            output_bin = self.output_bin.get().strip()
            if not output_bin:
                raise ValueError("--output-bin está habilitado pero vacío.")
            command.extend(["--output-bin", output_bin])

        if self.enable_output_pbm.get():
            output_pbm = self.output_pbm.get().strip()
            if not output_pbm:
                raise ValueError("--output-pbm está habilitado pero vacío.")
            command.extend(["--output-pbm", output_pbm])

        if not self.save_pbm.get():
            command.append("--no-save-pbm")

        if self.return_pbm.get():
            command.append("--return-pbm")
        else:
            # El cliente C generado previamente tiene --no-return-pbm para pruebas.
            # Si su versión no lo soporta, deje return-pbm activado o quite este argumento.
            command.append("--no-return-pbm")

        if self.return_packet.get():
            command.append("--return-packet")

        if self.no_run.get():
            command.append("--no-run")

        results_dir = self.results_dir.get().strip()
        if results_dir:
            command.extend(["--results-dir", results_dir])

        extra = self.custom_extra_args.get().strip()
        if extra:
            # Separación compatible con shell quoting básico.
            import shlex
            command.extend(shlex.split(extra))

        return command

    def _update_command_preview(self):
        try:
            command = self._build_command()
            preview = self._format_command(command)
        except Exception as exc:
            preview = f"ERROR construyendo comando: {exc}"

        self.command_preview.configure(state=tk.NORMAL)
        self.command_preview.delete("1.0", tk.END)
        self.command_preview.insert(tk.END, preview)
        self.command_preview.configure(state=tk.DISABLED)

    def _format_command(self, command):
        import shlex
        return " \\\n  ".join(shlex.quote(str(part)) for part in command)

    # ============================================================
    # State
    # ============================================================

    def _update_option_states(self):
        state_bin = tk.NORMAL if self.enable_output_bin.get() else tk.DISABLED
        state_pbm = tk.NORMAL if self.enable_output_pbm.get() else tk.DISABLED

        self.output_bin_entry.configure(state=state_bin)
        self.output_pbm_entry.configure(state=state_pbm)
        self._update_command_preview()

    def _set_running(self, running):
        if running:
            self.run_button.configure(state=tk.DISABLED)
            self.stop_button.configure(state=tk.NORMAL)
            self.progress.start(10)
            self.status_label.configure(text="Ejecutando pov_tcp_client...")
        else:
            self.run_button.configure(state=tk.NORMAL)
            self.stop_button.configure(state=tk.DISABLED)
            self.progress.stop()
            self.status_label.configure(text="Listo.")

    # ============================================================
    # Process execution
    # ============================================================

    def _start_process(self):
        if self.process is not None:
            messagebox.showwarning("Proceso en ejecución", "Ya hay un proceso ejecutándose.")
            return

        try:
            command = self._build_command()
        except Exception as exc:
            messagebox.showerror("Parámetros inválidos", str(exc))
            return

        cwd = self.working_dir.get().strip() or None

        if cwd and not Path(cwd).is_dir():
            messagebox.showerror("Directorio inválido", f"No existe el directorio:\n{cwd}")
            return

        self._clear_log()
        self._append_log("[COMMAND]\n")
        self._append_log(self._format_command(command) + "\n\n")
        self.notebook.select(self.log_frame)
        self._set_running(True)

        self.worker_thread = threading.Thread(
            target=self._run_process_thread,
            args=(command, cwd),
            daemon=True,
        )
        self.worker_thread.start()

    def _run_process_thread(self, command, cwd):
        try:
            self.process = subprocess.Popen(
                command,
                cwd=cwd,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                bufsize=1,
            )

            assert self.process.stdout is not None
            for line in self.process.stdout:
                self.output_queue.put(("log", line))

            return_code = self.process.wait()
            self.output_queue.put(("done", return_code))

        except Exception as exc:
            self.output_queue.put(("error", str(exc)))
        finally:
            self.process = None

    def _stop_process(self):
        if self.process is None:
            return

        if not messagebox.askyesno("Detener", "¿Desea detener el proceso actual?"):
            return

        try:
            self.process.terminate()
            self._append_log("\n[INFO] Proceso terminado por el usuario.\n")
        except Exception as exc:
            messagebox.showerror("Error", f"No se pudo detener el proceso:\n{exc}")

    def _poll_output_queue(self):
        try:
            while True:
                item_type, payload = self.output_queue.get_nowait()

                if item_type == "log":
                    self._append_log(payload)

                elif item_type == "done":
                    self._set_running(False)
                    self._handle_process_done(payload)

                elif item_type == "error":
                    self._set_running(False)
                    self._append_log(f"\n[ERROR] {payload}\n")
                    messagebox.showerror("Error ejecutando cliente", payload)

        except queue.Empty:
            pass

        self.root.after(100, self._poll_output_queue)

    def _handle_process_done(self, return_code):
        if return_code == 0:
            results_dir = self.results_dir.get().strip() or "client_results"
            output_bin = self.output_bin.get().strip() if self.enable_output_bin.get() else ""
            output_pbm = self.output_pbm.get().strip() if self.enable_output_pbm.get() else ""

            message = "El proceso terminó correctamente."

            if output_bin:
                message += f"\n\nBIN local:\n{output_bin}"

            if self.save_pbm.get() and output_pbm:
                message += f"\n\nPBM grande:\n{output_pbm}"

            if self.return_pbm.get() or self.return_packet.get():
                message += f"\n\nResultados descargados en:\n{results_dir}"

            self._append_log("\n[OK] Proceso finalizado correctamente.\n")
            messagebox.showinfo("Proceso completado", message)
        else:
            self._append_log(f"\n[ERROR] pov_tcp_client terminó con código {return_code}.\n")
            messagebox.showerror(
                "Proceso falló",
                f"pov_tcp_client terminó con código {return_code}. Revise la pestaña de logs.",
            )

    # ============================================================
    # Log helpers
    # ============================================================

    def _append_log(self, text):
        self.log_text.insert(tk.END, text)
        self.log_text.see(tk.END)

    def _clear_log(self):
        self.log_text.delete("1.0", tk.END)


def main():
    root = tk.Tk()
    app = PovTcpClientGui(root)
    root.mainloop()


if __name__ == "__main__":
    main()
