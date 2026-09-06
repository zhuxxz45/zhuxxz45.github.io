# -*- coding: utf-8 -*-
"""
Py转EXE图形工具 v1.0
基于 PyInstaller 的 .py -> .exe 图形化打包工具。

特性:
  * 选择 Python 脚本一键打包为 EXE
  * 可选隐藏控制台黑窗口（默认不开启）
  * 自定义图标：支持 .ico / .png / .jpg / .jpeg（自动转换为 ico）
  * 单文件模式 / 自定义输出目录 / 自定义程序名 / 附加参数

命令行模式（供自动化或排查使用）:
  Py转EXE工具.exe --build 脚本.py [--name 名称] [--icon 图标] [--noconsole]
                  [--outdir 目录] [--workdir 目录] [--onedir] [--extra "参数"]
"""
import argparse
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import threading
import traceback
from pathlib import Path

import tkinter as tk
from tkinter import ttk, filedialog, messagebox, scrolledtext

APP_NAME = "Py转EXE工具"
VERSION = "1.0.0"
ICON_EXTS = {".ico", ".png", ".jpg", ".jpeg"}


def strip_ansi(s):
    return re.sub(r"\x1b\[[0-9;]*m", "", s)


def safe_name(name):
    cleaned = re.sub(r'[<>:"/\\|?*\x00-\x1f]', "_", name).strip().strip(".")
    return cleaned or "output"


class BuildRunner:
    """打包执行器（GUI 与命令行共用）"""

    def __init__(self, emit):
        self.emit = emit

    def _prepare_icon(self, src: Path, work_dir: Path):
        suffix = src.suffix.lower()
        if suffix == ".ico":
            return src
        try:
            from PIL import Image
        except Exception:
            self.emit("图标转换需要 Pillow 库，请先点击「安装依赖」后再打包。")
            return None
        dst = work_dir / "app_icon.ico"
        try:
            img = Image.open(src).convert("RGBA")
            img.save(dst, format="ICO",
                     sizes=[(16, 16), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
            self.emit("已将 %s 图标自动转换为 .ico" % suffix)
            return dst
        except Exception as e:
            self.emit("图标转换失败: %s" % e)
            return None

    def run(self, script, name, outdir, icon, onefile=True, noconsole=False, extra="", workdir=None):
        pyi = shutil.which("pyinstaller")
        if not pyi:
            self.emit("未找到 PyInstaller，请先点击「安装依赖」或执行: pip install pyinstaller")
            return False, None
        script_path = Path(script).resolve()
        if not script_path.is_file():
            self.emit("脚本文件不存在: %s" % script_path)
            return False, None
        out_path = Path(outdir).resolve() if outdir else script_path.parent / "dist"
        out_path.mkdir(parents=True, exist_ok=True)
        work_dir = Path(workdir).resolve() if workdir else script_path.parent / ".py2exe_work"
        work_dir.mkdir(parents=True, exist_ok=True)

        cmd = [pyi, "--noconfirm", "--clean",
               "--specpath", str(work_dir),
               "--workpath", str(work_dir),
               "--distpath", str(out_path)]
        if onefile:
            cmd.append("--onefile")
        if noconsole:
            cmd.append("--noconsole")
        if icon and Path(icon).is_file():
            ico = self._prepare_icon(Path(icon), work_dir)
            if ico:
                cmd += ["--icon", str(ico)]
        cmd += ["--name", name]
        if extra and extra.strip():
            cmd += shlex.split(extra)
        cmd.append(str(script_path))

        self.emit("打包命令: " + " ".join(cmd))
        flags = subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0
        proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                text=True, encoding="utf-8", errors="replace",
                                bufsize=1, creationflags=flags)
        for line in proc.stdout:
            self.emit(strip_ansi(line.rstrip()))
        proc.wait()
        if proc.returncode != 0:
            self.emit("打包失败，退出码 %s（详见上方日志）" % proc.returncode)
            return False, None
        exe_path = out_path / (name + ".exe")
        if exe_path.exists():
            self.emit("打包成功: %s" % exe_path)
            return True, str(exe_path)
        self.emit("打包完成，但未找到目标文件: %s" % exe_path)
        return False, None


class Py2ExeApp:
    def __init__(self, root):
        self.root = root
        self.busy = False
        self.last_outdir = ""
        root.title("%s v%s" % (APP_NAME, VERSION))
        root.geometry("800x680")
        root.minsize(720, 600)
        self._ui()
        self.check_env()

    # ---------------- 界面 ----------------
    def _ui(self):
        try:
            style = ttk.Style()
            if "vista" in style.theme_names():
                style.theme_use("vista")
        except Exception:
            pass

        outer = ttk.Frame(self.root, padding=14)
        outer.pack(fill="both", expand=True)

        ttk.Label(outer, text="Python 脚本 → EXE 一键打包",
                  font=("Microsoft YaHei UI", 14, "bold")).pack(anchor="w")

        frm = ttk.LabelFrame(outer, text="打包设置", padding=10)
        frm.pack(fill="x", pady=(10, 6))

        def row(parent, label, var, btn_text=None, btn_cmd=None):
            r = ttk.Frame(parent)
            r.pack(fill="x", pady=4)
            ttk.Label(r, text=label, width=13).pack(side="left")
            ttk.Entry(r, textvariable=var).pack(side="left", fill="x", expand=True, padx=(0, 6))
            if btn_text:
                ttk.Button(r, text=btn_text, command=btn_cmd).pack(side="left")
            return r

        self.var_script = tk.StringVar()
        self.var_outdir = tk.StringVar()
        self.var_name = tk.StringVar()
        self.var_icon = tk.StringVar()
        self.var_extra = tk.StringVar()

        row(frm, "Python 脚本 *", self.var_script, "浏览...", self.browse_script)
        row(frm, "输出目录", self.var_outdir, "浏览...", self.browse_outdir)
        row(frm, "程序名称", self.var_name)
        ttk.Label(frm, text="（留空则使用脚本文件名）",
                  foreground="#888888").pack(anchor="e", padx=(13, 0))
        row(frm, "图标文件", self.var_icon, "浏览...", self.browse_icon)
        ttk.Label(frm, text="支持 .ico / .png / .jpg / .jpeg，png/jpg 将自动转换为 ico",
                  foreground="#888888").pack(anchor="e", padx=(13, 0))

        opt = ttk.LabelFrame(outer, text="选项", padding=10)
        opt.pack(fill="x", pady=(6, 6))
        self.var_onefile = tk.BooleanVar(value=True)
        self.var_noconsole = tk.BooleanVar(value=False)
        ttk.Checkbutton(opt, text="单文件模式（--onefile，推荐）",
                        variable=self.var_onefile).pack(anchor="w", pady=2)
        ttk.Checkbutton(opt, text="隐藏控制台黑窗口（--noconsole，默认不开启）",
                        variable=self.var_noconsole).pack(anchor="w", pady=2)
        r = ttk.Frame(opt)
        r.pack(fill="x", pady=4)
        ttk.Label(r, text="附加参数", width=13).pack(side="left")
        ttk.Entry(r, textvariable=self.var_extra).pack(side="left", fill="x", expand=True)

        btns = ttk.Frame(outer)
        btns.pack(fill="x", pady=6)
        self.btn_build = ttk.Button(btns, text="开始打包", command=self.start_build)
        self.btn_build.pack(side="left", padx=(0, 8))
        ttk.Button(btns, text="打开输出目录", command=self.open_outdir).pack(side="left", padx=(0, 8))
        self.btn_install = ttk.Button(btns, text="安装依赖", command=self.install_deps)
        self.btn_install.pack(side="left")

        self.status_var = tk.StringVar(value="正在检查环境...")
        ttk.Label(outer, textvariable=self.status_var,
                  foreground="#1a66c8").pack(anchor="w", pady=(4, 0))

        self.progress = ttk.Progressbar(outer, mode="indeterminate")
        self.progress.pack(fill="x", pady=(4, 6))

        ttk.Label(outer, text="打包日志：").pack(anchor="w")
        self.log_area = scrolledtext.ScrolledText(outer, height=14,
                                                  font=("Consolas", 9), state="disabled")
        self.log_area.pack(fill="both", expand=True)

    # ---------------- 事件 ----------------
    def browse_script(self):
        p = filedialog.askopenfilename(title="选择 Python 脚本",
                                       filetypes=[("Python 脚本", "*.py"), ("所有文件", "*.*")])
        if p:
            self.var_script.set(p)
            if not self.var_name.get().strip():
                self.var_name.set(Path(p).stem)

    def browse_outdir(self):
        p = filedialog.askdirectory(title="选择输出目录")
        if p:
            self.var_outdir.set(p)

    def browse_icon(self):
        p = filedialog.askopenfilename(
            title="选择图标（支持 ico/png/jpg/jpeg）",
            filetypes=[("图标文件", "*.ico *.png *.jpg *.jpeg"),
                       ("所有文件", "*.*")])
        if p:
            self.var_icon.set(p)

    def check_env(self):
        pyi = shutil.which("pyinstaller")
        has_pil = True
        try:
            import PIL  # noqa
        except Exception:
            has_pil = False
        if pyi:
            tip = "环境就绪：已检测到 PyInstaller"
            if not has_pil:
                tip += "；但缺少 Pillow（png/jpg 图标转换需要，建议点「安装依赖」）"
            self.status(tip)
        else:
            self.status("未检测到 PyInstaller，请点击「安装依赖」或手动执行 pip install pyinstaller")

    def status(self, text):
        self.status_var.set(text)

    def start_build(self):
        if self.busy:
            return
        script = self.var_script.get().strip()
        if not script:
            messagebox.showwarning(APP_NAME, "请先选择要打包的 Python 脚本。")
            return
        if not Path(script).is_file():
            messagebox.showwarning(APP_NAME, "脚本文件不存在：%s" % script)
            return
        icon = self.var_icon.get().strip()
        if icon and not Path(icon).is_file():
            messagebox.showwarning(APP_NAME, "图标文件不存在，请重新选择。")
            return
        name = safe_name(self.var_name.get().strip() or Path(script).stem)
        outdir = self.var_outdir.get().strip() or str(Path(script).parent / "dist")
        extra = self.var_extra.get().strip()
        onefile = self.var_onefile.get()
        noconsole = self.var_noconsole.get()
        self._set_busy(True)
        self.log_clear()
        threading.Thread(target=self._build_worker,
                         args=(script, name, outdir, icon, onefile, noconsole, extra),
                         daemon=True).start()

    def _build_worker(self, script, name, outdir, icon, onefile, noconsole, extra):
        try:
            ok, exe_path = BuildRunner(self._emit).run(
                script, name, outdir, icon, onefile, noconsole, extra)
            if ok:
                self.last_outdir = outdir
                self._emit(">>> 完成，已生成 EXE：%s" % exe_path)
                self.root.after(0, lambda: messagebox.showinfo(APP_NAME, "打包成功！\n\n%s" % exe_path))
            else:
                self.root.after(0, lambda: messagebox.showerror(APP_NAME, "打包失败，请查看下方日志。"))
        except Exception:
            self._emit(traceback.format_exc())
            self.root.after(0, lambda: messagebox.showerror(APP_NAME, "发生错误，请查看日志。"))
        finally:
            self.root.after(0, lambda: self._set_busy(False))

    def install_deps(self):
        if self.busy:
            return
        if not messagebox.askyesno(APP_NAME, "将执行：\n  pip install -U pyinstaller pillow\n需要联网下载，是否继续？"):
            return
        self._set_busy(True)
        self.log_clear()
        threading.Thread(target=self._install_worker, daemon=True).start()

    def _install_worker(self):
        try:
            py = shutil.which("python") or "python"
            cmd = [py, "-m", "pip", "install", "-U", "pyinstaller", "pillow"]
            flags = subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0
            proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                    text=True, encoding="utf-8", errors="replace",
                                    bufsize=1, creationflags=flags)
            for line in proc.stdout:
                self._emit(strip_ansi(line.rstrip()))
            proc.wait()
            if proc.returncode == 0:
                self._emit("依赖安装完成。")
                self.root.after(0, self.check_env)
            else:
                self._emit("依赖安装失败（退出码 %s）。" % proc.returncode)
        except Exception:
            self._emit(traceback.format_exc())
        finally:
            self.root.after(0, lambda: self._set_busy(False))

    def open_outdir(self):
        d = self.last_outdir or self.var_outdir.get().strip()
        if not d:
            messagebox.showinfo(APP_NAME, "还没有可打开的目录，请先完成一次打包。")
            return
        try:
            os.startfile(d)  # noqa
        except Exception as e:
            messagebox.showerror(APP_NAME, "打开目录失败：%s" % e)

    def _set_busy(self, busy):
        self.busy = busy
        state = "disabled" if busy else "normal"
        self.btn_build.config(state=state)
        self.btn_install.config(state=state)
        if busy:
            self.progress.start(10)
        else:
            self.progress.stop()

    def _emit(self, text):
        self.root.after(0, self._log, text)

    def _log(self, text):
        self.log_area.config(state="normal")
        self.log_area.insert("end", text + "\n")
        self.log_area.see("end")
        self.log_area.config(state="disabled")

    def log_clear(self):
        self.log_area.config(state="normal")
        self.log_area.delete("1.0", "end")
        self.log_area.config(state="disabled")


# ---------------- 命令行模式 ----------------
def cli_main(argv):
    parser = argparse.ArgumentParser(prog=APP_NAME, description="Py 转 EXE（命令行模式）")
    parser.add_argument("--build", metavar="SCRIPT", help="要打包的 .py 脚本")
    parser.add_argument("--name", default="", help="程序名（默认取脚本名）")
    parser.add_argument("--outdir", default="", help="输出目录（默认: 脚本目录/dist）")
    parser.add_argument("--workdir", default="", help="构建临时目录（默认: 脚本目录/.py2exe_work）")
    parser.add_argument("--icon", default="", help="图标文件（ico/png/jpg/jpeg）")
    parser.add_argument("--noconsole", action="store_true", help="隐藏控制台窗口")
    parser.add_argument("--console", dest="noconsole", action="store_false", help="显示控制台窗口（默认）")
    parser.set_defaults(noconsole=False)
    parser.add_argument("--onedir", action="store_true", help="多文件模式（默认单文件）")
    parser.add_argument("--extra", default="", help="附加 PyInstaller 参数")
    args = parser.parse_args(argv)
    if not args.build:
        parser.print_help()
        return 2
    name = safe_name(args.name or Path(args.build).stem)
    if sys.stdout is None:  # 无控制台环境
        def emit(s):
            try:
                with open("py2exe_build_log.txt", "a", encoding="utf-8") as f:
                    f.write(s + "\n")
            except Exception:
                pass
    else:
        emit = lambda s: print(s)  # noqa: E731
    ok, exe = BuildRunner(emit).run(args.build, name, args.outdir, args.icon,
                                    onefile=not args.onedir, noconsole=args.noconsole,
                                    extra=args.extra, workdir=args.workdir)
    return 0 if ok else 1


def selftest():
    info = {
        "frozen": bool(getattr(sys, "frozen", False)),
        "argv": sys.argv,
        "pyinstaller": shutil.which("pyinstaller"),
        "pillow": None,
    }
    try:
        import PIL
        info["pillow"] = PIL.__version__
    except Exception as e:
        info["pillow"] = "missing: %s" % e
    (Path.cwd() / "selftest_ok.txt").write_text(
        json.dumps(info, ensure_ascii=False, indent=2), encoding="utf-8")
    return 0


def main():
    if "--selftest" in sys.argv:
        return selftest()
    if "--build" in sys.argv:
        return cli_main(sys.argv[1:])
    root = tk.Tk()
    Py2ExeApp(root)
    root.mainloop()
    return 0


if __name__ == "__main__":
    sys.exit(main())
