"""Portable Tk desktop launcher. Game generation and progress remain on this PC."""
import json
import os
from pathlib import Path
import queue
import signal
import subprocess
import sys
import threading
import tkinter as tk
from tkinter import filedialog, messagebox, ttk

sys.dont_write_bytecode = True
from prepare_posix import APP, PAYLOAD, common, lock, package_root
from run_posix import selected_build
from ai_packs import install_selection, detach_selection
from disc_profile import profile_scenario

PROFILES = ('scenario1-usa-v1_001', 'scenario2-translation-v24', 'scenario3-translation-v25_1')
DATA_FOLDERS = ('Scenario1-USA', 'Scenario2-v24', 'Scenario3-v25_1', 'Scenario1-Translated')
SCENARIOS = (1,2,3,1)
DISPLAY_GAMES = (0,3,1,2)

def data_root():
    if sys.platform == 'darwin':
        return Path.home()/'Library/Application Support/SFIII Native'
    return Path(os.environ.get('XDG_DATA_HOME',Path.home()/'.local/share'))/'SFIII Native'


class Launcher:
    def __init__(self, root, config=None):
        self.root, self.process = root, None
        self.events = queue.Queue()
        self.config = Path(config) if config else data_root()/'launcher.json'
        self.preferences = dict(selected=0,games={})
        if self.config.is_file():
            self.preferences.update(json.loads(self.config.read_text()))
        root.title('Shining Force III Native')
        root.geometry('850x530')
        root.minsize(740,450)
        frame = ttk.Frame(root,padding=20)
        frame.pack(fill='both',expand=True)
        frame.columnconfigure(1,weight=1)
        ttk.Label(frame,text='Shining Force III Native',font=('',22,'bold')).grid(row=0,column=0,columnspan=3,sticky='w',pady=(0,18))
        self.selection = ttk.Combobox(frame,state='readonly',values=['Scenario 1 — original USA','Scenario 1 — translated (experimental)','Scenario 2 — translated (experimental)','Scenario 3 — translated (experimental)'])
        self.selection.grid(row=1,column=1,sticky='ew',pady=5)
        ttk.Label(frame,text='Game').grid(row=1,column=0,sticky='w',padx=(0,12))
        self.cue, self.work = tk.StringVar(), tk.StringVar()
        self.entries=[]
        for row,title,var,browse in [(2,'Disc (CUE / CHD)',self.cue,self.browse_disc),(3,'Game and saves',self.work,self.browse_work)]:
            ttk.Label(frame,text=title).grid(row=row,column=0,sticky='w',padx=(0,12))
            entry=ttk.Entry(frame,textvariable=var)
            entry.grid(row=row,column=1,sticky='ew',pady=5)
            button=ttk.Button(frame,text='Browse',command=browse)
            button.grid(row=row,column=2,padx=(10,0))
            self.entries.extend([entry,button])
        ttk.Label(frame,text='Saves and quick states are stored in this folder. Changing folders does not move them.',wraplength=780).grid(row=4,column=0,columnspan=3,sticky='w',pady=(2,0))
        bar=ttk.Frame(frame)
        bar.grid(row=5,column=0,columnspan=3,sticky='w',pady=15)
        self.prepare=ttk.Button(bar,text='Prepare game',command=self.prepare_game)
        self.play=ttk.Button(bar,text='Play',command=self.play_game)
        self.controller=ttk.Button(bar,text='Controller setup',command=lambda:self.controller_tool('--setup'))
        self.test=ttk.Button(bar,text='Test controller',command=lambda:self.controller_tool('--test'))
        self.import_button=ttk.Button(bar,text='Save manager',command=self.import_saves)
        self.cancel=ttk.Button(bar,text='Cancel setup',command=self.cancel_setup)
        for button in [self.prepare,self.play,self.controller,self.test,self.import_button,self.cancel]:button.pack(side='left',padx=(0,7))
        artwork=ttk.Frame(frame)
        artwork.grid(row=6,column=0,columnspan=3,sticky='w')
        self.artwork=ttk.Button(artwork,text='Attach optional graphics',command=self.attach_artwork)
        self.artwork.pack(side='left',padx=(0,7))
        self.original_art=ttk.Button(artwork,text='Use original graphics',command=self.detach_artwork)
        self.original_art.pack(side='left',padx=(0,7))
        self.open_saves=ttk.Button(artwork,text='Open save folder',command=self.open_save_folder)
        self.open_saves.pack(side='left')
        self.state=tk.StringVar(value='Choose your supported disc, then prepare the game.')
        ttk.Label(frame,textvariable=self.state,wraplength=780).grid(row=7,column=0,columnspan=3,sticky='w',pady=5)
        self.progress=ttk.Progressbar(frame,mode='indeterminate')
        self.progress.grid(row=8,column=0,columnspan=3,sticky='ew',pady=10)
        ttk.Label(frame,text='Experimental Linux/macOS preview. Keep a backup of your game-data folder.\nNo game assets or BIOS included. Setup and compilation run locally with bundled tools.',wraplength=780).grid(row=9,column=0,columnspan=3,sticky='w')
        self.active=max(0,min(len(SCENARIOS)-1,int(self.preferences.get('selected',0))))
        self.selection.current(DISPLAY_GAMES.index(self.active))
        self.restore()
        self.selection.bind('<<ComboboxSelected>>',self.change_game)
        root.protocol('WM_DELETE_WINDOW',self.close)
        root.after(100,self.poll)

    def remember(self):
        self.preferences['selected']=self.active
        self.preferences['games'][str(self.active)]=dict(cue=self.cue.get(),work=self.work.get())
        self.config.parent.mkdir(parents=True,exist_ok=True)
        common.atomic_json(self.config,self.preferences)

    def restore(self):
        values=self.preferences['games'].get(str(self.active),{})
        self.cue.set(values.get('cue',''))
        self.work.set(values.get('work',str(data_root()/DATA_FOLDERS[self.active])))
        self.refresh()

    def change_game(self,event=None):
        self.remember()
        self.active=DISPLAY_GAMES[self.selection.current()]
        self.restore()
        self.remember()

    def ready(self):
        try:
            work=Path(self.work.get())
            ready=json.loads((work/'desktop-ready.json').read_text())
            package=json.loads((PAYLOAD/'package.json').read_text())
            scenario=profile_scenario(ready['profile_id'],APP/'profiles')
            if scenario is None:return False
            slot=3 if scenario==1 and ready['profile_id']!=PROFILES[0] else scenario-1
            return (ready['build_id']==package['build_id'] and slot==self.active
                    and common.sha(selected_build(work)/'saturn')==ready['executable_sha256'])
        except (OSError,ValueError,KeyError):return False

    def refresh(self):
        busy=self.process is not None
        ready=not busy and self.ready()
        for button in [self.play,self.controller,self.test,self.import_button,self.artwork,self.original_art]:button.configure(state='normal' if ready else 'disabled')
        self.prepare.configure(state='disabled' if busy else 'normal')
        self.cancel.configure(state='normal' if busy and self.task=='prepare' else 'disabled')
        self.selection.configure(state='disabled' if busy else 'readonly')
        for widget in self.entries:widget.configure(state='disabled' if busy else 'normal')

    def browse_disc(self):
        path=filedialog.askopenfilename(filetypes=[('Saturn images','*.cue *.CUE *.chd *.CHD'),('All files','*')])
        if path:self.cue.set(path)
        self.remember()
        self.refresh()

    def browse_work(self):
        path=filedialog.askdirectory(title='Choose the folder for this game and its saves')
        if path:self.work.set(path)
        self.remember()
        self.refresh()

    def open_save_folder(self):
        path=Path(self.work.get()).expanduser()
        if not self.work.get().strip() or not path.is_dir():
            messagebox.showinfo('Save folder','This folder does not exist yet. It will be created when you prepare the game.',parent=self.root)
            return
        try:
            subprocess.Popen(['open' if sys.platform=='darwin' else 'xdg-open',str(path.resolve())],start_new_session=True)
        except OSError as error:
            messagebox.showerror('Could not open save folder',str(error),parent=self.root)

    def start(self,args,task,env=None):
        self.remember()
        self.task=task
        self.state.set('Preparing...' if task=='prepare' else 'Running '+task+'. Close the game/tool window when finished.')
        self.process=subprocess.Popen(list(map(str,args)),stdout=subprocess.PIPE,stderr=subprocess.STDOUT,
                                      text=True,encoding='utf-8',errors='replace',start_new_session=True,env=env)
        self.progress.start()
        self.refresh()
        def read(process):
            lines=[]
            for line in process.stdout:
                lines.append(line)
                if line.startswith('SF3_STATUS '):self.events.put(('status',line[11:]))
            self.events.put(('done',(process.wait(),''.join(lines))))
        threading.Thread(target=read,args=(self.process,),daemon=True).start()

    def prepare_game(self):
        self.start([PAYLOAD/'python/bin/python3','-X','utf8','-B',APP/'desktop/prepare_posix.py',
                    '--cue',self.cue.get(),'--work',self.work.get(),'--scenario',str(SCENARIOS[self.active])],'prepare')

    def play_game(self):
        self.start([PAYLOAD/'python/bin/python3','-X','utf8','-B',APP/'desktop/run_posix.py','--work',self.work.get()],'game')

    def attach_artwork(self):
        report=filedialog.askopenfilename(title='Select locally generated packs-report.json',filetypes=[('Artwork pack report','*.json')])
        if not report:return
        try:
            with lock(Path(self.work.get())):
                install_selection(Path(report),Path(self.work.get()),APP/'profiles')
            self.state.set('Optional artwork attached. Select AI graphics in the game menu. Menus and unmatched artwork stay original.')
        except (OSError,ValueError,KeyError,TypeError) as error:
            messagebox.showerror('Could not attach artwork',str(error),parent=self.root)

    def detach_artwork(self):
        try:
            with lock(Path(self.work.get())):detach_selection(Path(self.work.get()))
            self.state.set('Original graphics selected. Previous artwork selection was preserved.')
        except (OSError,ValueError) as error:
            messagebox.showerror('Could not change artwork',str(error),parent=self.root)

    def controller_tool(self,mode):
        env={**os.environ,'SF3_CONTROLLER_DIR':self.work.get()}
        self.start([selected_build(Path(self.work.get()))/'controller_setup',mode],'controller',env)

    def import_saves(self):
        action=messagebox.askyesnocancel('Save manager','Export ordinary saves as BIN files?\nYes: Export for Saturn / Saroo editor\nNo: Import BIN / YSBAK saves',parent=self.root)
        if action is None:return
        if action:
            folder=filedialog.askdirectory(title='Choose export location')
            if not folder:return
            device='cartridge' if messagebox.askyesno('Save storage','Export from cartridge memory?\nNo selects internal memory.',parent=self.root) else 'internal'
            try:
                import importlib.util,time
                spec=importlib.util.spec_from_file_location('sf3_import',APP/'tools/import-saturn-save.py')
                module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
                work=common.external_work(Path(self.work.get()),package_root())
                destination=Path(folder)/('SFIII-save-export-'+str(time.time_ns()))
                with lock(work):module.export_files(work/'experimental-backup.bin',device,destination)
                messagebox.showinfo('Export complete','BIN saves exported to '+str(destination),parent=self.root)
            except Exception as e:messagebox.showerror('Could not export',str(e),parent=self.root)
            return
        paths=filedialog.askopenfilenames(filetypes=[('Saturn saves','*.BIN *.bin *.ysbak'),('All files','*')])
        if not paths:return
        dialog=tk.Toplevel(self.root)
        dialog.title('Import Saturn saves')
        ttk.Label(dialog,text='Destination storage (existing data is backed up)').pack(padx=20,pady=10)
        choice=ttk.Combobox(dialog,state='readonly',values=['cartridge','internal'])
        choice.current(0)
        choice.pack(padx=20,pady=10)
        replace=tk.BooleanVar(value=False)
        ttk.Checkbutton(dialog,text='Replace matching save names (backup retained)',variable=replace).pack(padx=20,pady=10)
        def apply():
            try:
                import importlib.util
                spec=importlib.util.spec_from_file_location('sf3_import',APP/'tools/import-saturn-save.py')
                module=importlib.util.module_from_spec(spec)
                spec.loader.exec_module(module)
                work=common.external_work(Path(self.work.get()),package_root())
                with lock(work):module.import_files(work/'experimental-backup.bin',choice.get(),list(map(Path,paths)),replace.get())
                self.state.set('Saves imported. Use the game’s own Load menu.')
                dialog.destroy()
            except Exception as e:messagebox.showerror('Could not import',str(e),parent=dialog)
        ttk.Button(dialog,text='Import',command=apply).pack(padx=20,pady=15)

    def cancel_setup(self):
        if self.process and self.task=='prepare':
            os.killpg(self.process.pid,signal.SIGTERM)
            self.state.set('Setup cancelled; previous builds and ordinary saves are retained.')

    def poll(self):
        while not self.events.empty():
            event,data=self.events.get()
            if event=='status':self.state.set(json.loads(data)['message'])
            else:
                code,text=data
                self.process=None
                self.progress.stop()
                self.state.set('Ready.' if code==0 else text[-800:] or 'Stopped; inspect the local session/setup log.')
                self.refresh()
        self.root.after(100,self.poll)

    def close(self):
        if self.process:
            messagebox.showinfo('Still running','Close the game/tool window or cancel setup before closing the launcher.')
        else:
            self.remember()
            self.root.destroy()


if __name__=='__main__':
    root=tk.Tk()
    if len(sys.argv)==3 and sys.argv[1]=='--self-test':
        ui=Launcher(root,Path(sys.argv[2])/'launcher.json')
        root.update()
        assert str(ui.play['state'])=='disabled'
        ui.cue.set('/synthetic/one.cue')
        ui.work.set('/synthetic/one')
        ui.selection.current(1)
        ui.change_game()
        assert ui.cue.get()=='' and ui.work.get()!='/synthetic/one'
        ui.selection.current(0)
        ui.change_game()
        assert ui.cue.get()=='/synthetic/one.cue'
        ui.close()
    else:
        Launcher(root)
        root.mainloop()
