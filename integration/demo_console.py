"""Demo 专属隐藏控制台，仅由 publication.py 以 CREATE_NEW_CONSOLE 启动。

Popen 持有创建时取得的子进程句柄；仅在该句柄仍存活时向本桥专属控制台
发送 Ctrl+C，不连接调用方终端、不按进程名查找、不按可复用 PID 终止进程。
"""
import ctypes, subprocess, sys, threading
kernel = ctypes.WinDLL('kernel32',use_last_error=True)
Handler=ctypes.WINFUNCTYPE(ctypes.c_bool,ctypes.c_uint)
handler=Handler(lambda event: True)
if not kernel.SetConsoleCtrlHandler(handler,True): raise ctypes.WinError(ctypes.get_last_error())
child=subprocess.Popen(sys.argv[1:],stdin=subprocess.DEVNULL,stdout=sys.stdout,stderr=sys.stderr)
def commands():
 for line in sys.stdin:
  if line.strip()=='PST_DEMO_CTRL_C':
   if child.poll() is not None: return
   if not kernel.GenerateConsoleCtrlEvent(0,0):
    print('PST_CTRL_C_FAILED '+str(ctypes.get_last_error()),flush=True)
   else: print('PST_CTRL_C_SENT '+str(child.pid),flush=True)
threading.Thread(target=commands,daemon=True).start()
print('PST_DEMO_CHILD '+str(child.pid),flush=True)
raise SystemExit(child.wait())
