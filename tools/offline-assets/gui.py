"""Local desktop controls for the standalone offline graphics worker."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tkinter as tk
from tkinter import filedialog, messagebox, ttk

APP=Path(__file__).resolve().parent
ROOT=APP.parent
CONFIG=ROOT/'settings.json'
STYLE_LABELS={'video':'Animated', 'anime':'Illustrated', 'general':'General'}
STYLE_KEYS={label:key for key,label in STYLE_LABELS.items()}


def main():
    settings=json.loads(CONFIG.read_text(encoding='utf-8-sig'))
    window=tk.Tk();window.title('SFIII Offline Graphics Builder');window.geometry('1000x820')
    icon=APP/'app.ico'
    if icon.exists():window.iconbitmap(str(icon))
    frame=ttk.Frame(window,padding=18);frame.pack(fill='both',expand=True)
    ttk.Label(frame,text='SFIII Offline Graphics Builder',font=('Segoe UI',19,'bold')).pack(anchor='w')
    ttk.Label(frame,text='Runs entirely on this PC. Codex can be closed. Completed work is cached for resume.').pack(anchor='w',pady=(6,16))
    paths={}
    for number in (1,2,3):
        row=ttk.Frame(frame);row.pack(fill='x',pady=4)
        ttk.Label(row,text=f'Scenario {number}',width=13).pack(side='left')
        source=next((x['disc'] for x in settings['scenarios'] if x['scenario']==number),'')
        value=tk.StringVar(value=source);paths[number]=value
        ttk.Entry(row,textvariable=value).pack(side='left',fill='x',expand=True,padx=6)
        def browse(v=value):
            path=filedialog.askopenfilename(title='Select your local disc',filetypes=[('Saturn disc','*.cue *.chd'),('All files','*.*')])
            if path:v.set(path)
        ttk.Button(row,text='Browse',command=browse).pack(side='left')
    row=ttk.Frame(frame);row.pack(fill='x',pady=(14,4))
    ttk.Label(row,text='Output folder',width=13).pack(side='left')
    output=tk.StringVar(value=settings['out']);ttk.Entry(row,textvariable=output).pack(side='left',fill='x',expand=True,padx=6)
    def choose_output():
        path=filedialog.askdirectory(title='Choose a local output folder')
        if path:output.set(path)
    ttk.Button(row,text='Browse',command=choose_output).pack(side='left')
    options=ttk.Frame(frame);options.pack(fill='x',pady=12)
    ttk.Label(options,text='GPU index').pack(side='left')
    gpu=tk.StringVar(value=str(settings.get('gpu',0)))
    ttk.Combobox(options,textvariable=gpu,values=['0','1','2','3'],state='readonly',width=4).pack(side='left',padx=(8,20))
    ttk.Label(options,text='Styles').pack(side='left')
    styles=tk.StringVar(value='All three' if len(settings.get('models',[]))==3 else STYLE_LABELS.get(next(iter(settings.get('models',[])),'video'),'Animated'))
    ttk.Combobox(options,textvariable=styles,values=['All three',*STYLE_KEYS],state='readonly',width=14).pack(side='left',padx=8)
    extras=tk.BooleanVar(value=True)
    ttk.Checkbutton(frame,text='Include character sprites and battle-model textures; also cache backgrounds',variable=extras).pack(anchor='w')
    ttk.Label(frame,text='Choose Animated, Illustrated or General, or build all three.\nKeep All three styles selected to attach finished packs to a game. Menus stay original. Failed or unsupported assets remain original.',wraplength=950).pack(anchor='w',pady=(8,12))
    buttons=ttk.Frame(frame);buttons.pack(fill='x')
    def persist():
        scenarios=[dict(scenario=n,disc=v.get().strip()) for n,v in paths.items() if v.get().strip()]
        if not scenarios:raise ValueError('Select at least one disc')
        for item in scenarios:
            if not Path(item['disc']).is_file():raise ValueError('Disc not found: '+item['disc'])
        folder=Path(output.get()).resolve()
        if any((p/'.git').exists() for p in (folder,*folder.parents)):raise ValueError('Choose an output folder outside a source repository')
        current=dict(scenarios=scenarios,out=str(folder),gpu=int(gpu.get()),
            models=['video','anime','general'] if styles.get()=='All three' else [STYLE_KEYS[styles.get()]],
            categories=['world','sprites','battle_models','backgrounds'] if extras.get() else ['world'])
        CONFIG.write_text(json.dumps(current,indent=2)+'\n',encoding='utf-8');folder.mkdir(parents=True,exist_ok=True)
        return folder
    def start(inventory=False,quick=False):
        try:
            folder=persist();command=[str(ROOT/'runtime'/'python.exe'),str(APP/'worker.py'),'--config',str(CONFIG),'--out',str(folder),'--app',str(APP)]
            if inventory:command.append('--inventory-only')
            if quick:command.extend(['--sample-files','1','--limit','8'])
            with (folder/'builder.log').open('ab') as log:
                subprocess.Popen(command,stdout=log,stderr=subprocess.STDOUT,cwd=str(ROOT),
                    creationflags=0x08000000|subprocess.CREATE_NEW_PROCESS_GROUP if os.name=='nt' else 0)
            status.set('Worker launched. You may close this window or Codex; processing continues locally.')
        except Exception as error:messagebox.showerror('Could not start',str(error))
    def stop():
        folder=Path(output.get())
        if folder.is_dir():(folder/'stop.request').write_text('stop\n');status.set('Stopping the local worker safely…')
    def open_results():
        folder=Path(output.get())
        if folder.is_dir():os.startfile(folder)
    ttk.Button(buttons,text='Start / Resume',command=start).pack(side='left')
    ttk.Button(buttons,text='Quick check',command=lambda:start(quick=True)).pack(side='left',padx=(8,0))
    ttk.Button(buttons,text='Extract only (no GPU)',command=lambda:start(True)).pack(side='left',padx=8)
    ttk.Button(buttons,text='Stop safely',command=stop).pack(side='left')
    ttk.Button(buttons,text='Open output / reports',command=open_results).pack(side='right')
    pack_buttons=ttk.Frame(frame);pack_buttons.pack(fill='x',pady=(10,0))
    def attach_packs():
        folder=Path(output.get())
        report=folder/'playable-packs-report.json'
        if not report.is_file():report=folder/'packs-report.json'
        if not report.is_file():
            messagebox.showerror('Packs not ready','Finish Start / Resume before attaching packs.');return
        game=filedialog.askdirectory(title='Select the prepared game-data folder containing game.json')
        if not game:return
        try:
            result=subprocess.run([str(ROOT/'runtime/python.exe'),str(APP/'ai_packs.py'),'attach',
                '--report',str(report),'--game-dir',game,'--profiles',str(APP/'profiles')],
                capture_output=True,text=True,creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
            if result.returncode:raise ValueError((result.stderr or result.stdout).strip())
            messagebox.showinfo('Optional graphics attached','Restart the game, then choose an AI graphics style in its menu.\n'+result.stdout.strip())
        except Exception as error:messagebox.showerror('Could not attach packs',str(error))
    def detach_packs():
        game=filedialog.askdirectory(title='Select the game-data folder to return to original graphics')
        if not game:return
        try:
            result=subprocess.run([str(ROOT/'runtime/python.exe'),str(APP/'ai_packs.py'),'detach','--game-dir',game],
                capture_output=True,text=True,creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
            if result.returncode:raise ValueError((result.stderr or result.stdout).strip())
            messagebox.showinfo('Optional graphics detached','Restart the game to use original graphics. Packs and saves are preserved.')
        except Exception as error:messagebox.showerror('Could not detach packs',str(error))
    ttk.Button(pack_buttons,text='Attach packs to game',command=attach_packs).pack(side='left')
    ttk.Button(pack_buttons,text='Return game to original graphics',command=detach_packs).pack(side='left',padx=8)
    status=tk.StringVar(value='Ready. Start runs extraction, GPU processing, and pack generation locally.')
    ttk.Label(frame,textvariable=status,wraplength=950).pack(anchor='w',pady=(16,8))
    bar=ttk.Progressbar(frame,mode='determinate');bar.pack(fill='x')
    logbox=tk.Text(frame,height=12,font=('Consolas',9),state='disabled',wrap='word');logbox.pack(fill='both',expand=True,pady=12)
    def poll():
        folder=Path(output.get())
        try:
            report=json.loads((folder/'status.json').read_text())
            state=report.get('state','');message=report.get('message','')
            done,total=report.get('completed',0),report.get('total',0)
            status.set(f'{state}: {message}'+(f'  ({done}/{total})' if total else ''))
            bar['maximum']=max(total,1);bar['value']=done
            with (folder/'builder.log').open('rb') as log:
                log.seek(0,2);end=log.tell();log.seek(max(0,end-10000));tail=log.read().decode('utf-8',errors='replace')
            logbox.configure(state='normal');logbox.delete('1.0','end');logbox.insert('end',tail);logbox.see('end');logbox.configure(state='disabled')
        except (OSError,ValueError):pass
        window.after(1500,poll)
    ttk.Label(frame,text='Closing this window leaves the worker running. Use Stop safely before changing discs or settings.').pack(anchor='w')
    window.after(500,poll)
    if '--smoke-test' in sys.argv:window.after(700,window.destroy)
    window.mainloop()


if __name__=='__main__':main()
