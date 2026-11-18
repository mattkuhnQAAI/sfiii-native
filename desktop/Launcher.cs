// Windows first-run UI. Preparation and game data remain local to the player's PC.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Security.Cryptography;
using System.Text;
using System.Web.Script.Serialization;
using System.Windows.Forms;

class Launcher : Form {
    static readonly string[] Profiles = {"scenario1-usa-v1_001", "scenario2-translation-v24", "scenario3-translation-v25_1"};
    int ProfileScenario(string identifier) {
        string folder=Path.Combine(root,"payload","app","profiles");
        if(!Directory.Exists(folder))return Array.IndexOf(Profiles,identifier)+1;
        int result=0;
        foreach(string file in Directory.GetFiles(folder,"*.json")) {
            var profile=Read(file);
            if((string)profile["id"]!=identifier)continue;
            if(result!=0)throw new Exception("Duplicate revision profile ID");
            result=Convert.ToInt32(profile["scenario"]);
        }
        return result;
    }
    // Stable stored keys retain older users' Scenario 2/3 folders.
    static readonly int[] Scenarios = {1,2,3,1};
    static readonly int[] DisplayGames = {0,3,1,2};
    static readonly string[] DataSuffixes = {"", "-Scenario2-v24", "-Scenario3-v25_1", "-Scenario1-Translated"};
    int ProfileGame(string identifier) {
        int scenario=ProfileScenario(identifier);
        return scenario<1 || scenario>3 ? -1 : scenario==1 && identifier!=Profiles[0] ? 3 : scenario-1;
    }
    void SelectGame(int key) {gameChoice.SelectedIndex=Array.IndexOf(DisplayGames,key);}
    readonly string root = AppDomain.CurrentDomain.BaseDirectory;
    readonly JavaScriptSerializer json = new JavaScriptSerializer();
    readonly TextBox cue = new TextBox(), work = new TextBox();
    readonly Button selectDisc = new Button(), selectWork = new Button(), prepare = new Button(), play = new Button(), cancel = new Button(), controller = new Button(), import = new Button(), openFolder = new Button(), graphics = new Button(), originalGraphics = new Button();
    readonly Label state = new Label();
    readonly ComboBox gameChoice = new ComboBox();
    int activeGame;
    string defaultDataRoot;
    readonly ProgressBar progress = new ProgressBar();
    Process worker, game;
    string settingsPath, fallbackSettingsPath, packageId;
    bool busy;
    bool cancelled;
    string lastError;
    string smokePlayRoot;

    static string Quote(string value) {
        StringBuilder b = new StringBuilder("\""); int slashes = 0;
        foreach(char ch in value) {
            if(ch == '\\') {++slashes; continue;}
            if(ch == '"') {b.Append('\\',slashes*2+1); b.Append('"');}
            else {b.Append('\\',slashes); b.Append(ch);}
            slashes=0;
        }
        b.Append('\\',slashes*2); return b.Append('"').ToString();
    }
    static string Hash(string path) {
        using(var f=File.OpenRead(path)) using(var h=SHA256.Create()) return BitConverter.ToString(h.ComputeHash(f)).Replace("-","").ToLowerInvariant();
    }
    Dictionary<string,object> Read(string path) {return json.Deserialize<Dictionary<string,object>>(File.ReadAllText(path));}
    Dictionary<string,object> ReadSavedSettings() {
        return Read(File.Exists(settingsPath) || String.IsNullOrEmpty(fallbackSettingsPath) ? settingsPath : fallbackSettingsPath);
    }
    void Ui(Action action) {if(!IsDisposed && IsHandleCreated) BeginInvoke(action);}
    Label TextAt(string text,int x,int y,int width,int height) {
        var label=new Label {Text=text,Left=x,Top=y,Width=width,Height=height}; Controls.Add(label);return label;
    }
    void ButtonAt(Button button,string text,int x,int y,int width,EventHandler click) {
        button.Text=text;button.SetBounds(x,y,width,34);button.Click+=click;Controls.Add(button);
    }
    public Launcher(string testRoot) {
        Icon=Icon.ExtractAssociatedIcon(Application.ExecutablePath);
        Text="Shining Force III Native"; ClientSize=new Size(850,455);MinimumSize=Size;MaximumSize=Size;
        Font=new Font("Segoe UI",10);StartPosition=FormStartPosition.CenterScreen;FormBorderStyle=FormBorderStyle.FixedSingle;MaximizeBox=false;
        TextAt("Shining Force III Native",24,20,800,35).Font=new Font("Segoe UI",20,FontStyle.Bold);
        TextAt("Game",25,65,120,25);
        gameChoice.DropDownStyle=ComboBoxStyle.DropDownList;gameChoice.SetBounds(155,62,667,28);
        gameChoice.Items.AddRange(new object[]{"Scenario 1 — original USA","Scenario 1 — translated (experimental)","Scenario 2 — translated (experimental)","Scenario 3 — translated (experimental)"});
        gameChoice.SelectedIndex=0;Controls.Add(gameChoice);
        gameChoice.DrawMode=DrawMode.OwnerDrawFixed;
        gameChoice.DrawItem+=delegate(object sender,DrawItemEventArgs e) {
            e.DrawBackground();if(e.Index>=0)using(var brush=new SolidBrush(e.ForeColor))
                e.Graphics.DrawString(gameChoice.Items[e.Index].ToString(),Font,brush,e.Bounds);
            e.DrawFocusRectangle();
        };
        TextAt("Choose your disc once. The app prepares it automatically, then you can play.\nNo compiler installation or commands needed. Your disc files stay on your PC.",25,99,790,48);
        TextAt("Disc CUE / CHD",25,160,120,25);cue.SetBounds(155,157,535,28);Controls.Add(cue);
        ButtonAt(selectDisc,"Browse...",702,154,120,delegate {
            using(var d=new OpenFileDialog {Filter="Saturn disc image (*.cue;*.chd)|*.cue;*.chd",Title="Choose your own supported Saturn disc"}) if(d.ShowDialog(this)==DialogResult.OK) cue.Text=d.FileName;
        });
        TextAt("Game and saves",25,206,125,25);work.SetBounds(155,203,535,28);Controls.Add(work);
        ButtonAt(selectWork,"Choose...",702,200,120,delegate {
            using(var d=new FolderBrowserDialog {Description="Choose the folder for this game's saves, quick states and prepared files. Choosing a different folder does not move existing saves.",SelectedPath=work.Text}) if(d.ShowDialog(this)==DialogResult.OK) {work.Text=d.SelectedPath;RefreshReady();}
        });
        TextAt("Saves and quick states are stored in this folder. Changing folders does not move them.\nPreparation can take several minutes and needs several GB of free space.",25,239,795,40);
        state.SetBounds(25,281,795,25);Controls.Add(state);progress.SetBounds(25,312,795,14);Controls.Add(progress);
        ButtonAt(prepare,"Prepare game",25,346,140,Prepare);
        ButtonAt(play,"Play",175,346,80,Play);
        ButtonAt(controller,"Controller setup",265,346,155,delegate {LaunchControllerSetup();});
        ButtonAt(import,"Save manager",430,346,125,delegate {LaunchScript("import-saturn-save.ps1",true);});
        ButtonAt(openFolder,"Open save folder",565,346,145,delegate {if(Directory.Exists(work.Text)) Process.Start("explorer.exe",Quote(work.Text)); else MessageBox.Show(this,"This folder does not exist yet. It will be created when you prepare the game.","Save folder");});
        ButtonAt(cancel,"Cancel",720,346,100,delegate {CancelWorker();});
        ButtonAt(graphics,"Optional graphics...",25,397,180,delegate {SelectGraphics();});
        ButtonAt(originalGraphics,"Use original graphics",215,397,200,delegate {RunGraphicsTool(null);});
        TextAt("Optional local artwork; build packs from your own disc.",430,403,395,34);
        fallbackSettingsPath=Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),"SFIII Native","launcher.json");
        settingsPath=Path.Combine(root,"launcher-settings.json");
        work.Text=Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),"SFIII Native","Game");
        if(testRoot!=null) {settingsPath=Path.Combine(testRoot,"launcher.json");fallbackSettingsPath=null;work.Text=Path.Combine(testRoot,"Game");}
        defaultDataRoot=work.Text;
        try {var s=ReadSavedSettings();cue.Text=(string)s["cue"];work.Text=(string)s["work"];
            if(s.ContainsKey("selected")) {activeGame=Math.Max(0,Math.Min(Scenarios.Length-1,Convert.ToInt32(s["selected"])));SelectGame(activeGame);}
        } catch { }
        gameChoice.SelectedIndexChanged+=delegate {
            if(busy)return;SaveSettings();activeGame=DisplayGames[gameChoice.SelectedIndex];busy=true;
            cue.Text="";work.Text=defaultDataRoot+DataSuffixes[activeGame];
            try {var s=ReadSavedSettings();var games=(Dictionary<string,object>)s["games"];
                var entry=(Dictionary<string,object>)games[activeGame.ToString()];cue.Text=(string)entry["cue"];work.Text=(string)entry["work"];
            } catch { }
            busy=false;SaveSettings();RefreshReady();
        };
        try {packageId=(string)Read(Path.Combine(root,"payload","package.json"))["build_id"];} catch {state.Text="Extract the complete application ZIP before running it.";prepare.Enabled=false;}
        cue.TextChanged+=delegate {if(!busy)RefreshReady();};
        work.TextChanged+=delegate {if(!busy)RefreshReady();};
        Shown+=delegate {RefreshReady();};
        FormClosing+=delegate(object sender,FormClosingEventArgs e) {
            if(busy && MessageBox.Show(this,"Cancel preparation and close? Existing saves will be kept.","Preparation is running",MessageBoxButtons.YesNo)!=DialogResult.Yes) {e.Cancel=true;return;}
            CancelWorker();
        };
    }
    void SaveSettings() {
        Directory.CreateDirectory(Path.GetDirectoryName(settingsPath));
        Dictionary<string,object> games=new Dictionary<string,object>();
        try {games=(Dictionary<string,object>)ReadSavedSettings()["games"];} catch { }
        games[activeGame.ToString()]=new Dictionary<string,object>{{"cue",cue.Text},{"work",work.Text}};
        string text=json.Serialize(new Dictionary<string,object>{{"cue",cue.Text},{"work",work.Text},{"selected",activeGame},{"games",games}});
        try {File.WriteAllText(settingsPath,text);} catch(Exception error) {
            if(String.IsNullOrEmpty(fallbackSettingsPath) || !(error is IOException || error is UnauthorizedAccessException))throw;
            Directory.CreateDirectory(Path.GetDirectoryName(fallbackSettingsPath));File.WriteAllText(fallbackSettingsPath,text);
        }
    }
    bool Ready() {
        try {
            var prepared=Read(Path.Combine(work.Text,"desktop-ready.json"));
            string preparedCue=(string)prepared["cue"];
            string source=prepared.ContainsKey("source") ? (string)prepared["source"] : preparedCue;
            if(!prepared.ContainsKey("profile_id") && activeGame!=0)return false;
            if(prepared.ContainsKey("profile_id") && ProfileGame((string)prepared["profile_id"])!=activeGame)return false;
            if(String.IsNullOrWhiteSpace(cue.Text))cue.Text=source;
            if(!String.Equals(Path.GetFullPath(cue.Text),Path.GetFullPath(source),StringComparison.OrdinalIgnoreCase))return false;
            string selected=File.ReadAllText(Path.Combine(work.Text,"native-build.txt")).Trim();
            string expected=prepared.ContainsKey("build_directory") ? (string)prepared["build_directory"] : "build-desktop-"+packageId;
            if(!System.Text.RegularExpressions.Regex.IsMatch(expected,@"^build-desktop-[A-Za-z0-9_-]+$") || selected!=expected || (string)prepared["build_id"]!=packageId)return false;
            string exe=Path.Combine(work.Text,selected,"saturn.exe");
            return File.Exists(source) && File.Exists(preparedCue) && Hash(exe)==(string)prepared["executable_sha256"];
        } catch {return false;}
    }
    void RefreshReady() {
        if(busy)return;
        bool ready=Ready(),playing=game!=null;
        play.Enabled=ready && !playing;controller.Enabled=ready && !playing;import.Enabled=ready && !playing;
        graphics.Enabled=ready && !playing;originalGraphics.Enabled=ready && !playing && File.Exists(Path.Combine(work.Text,"ai-packs.json"));
        gameChoice.Enabled=!playing;prepare.Enabled=packageId!=null && !playing;selectDisc.Enabled=selectWork.Enabled=cue.Enabled=work.Enabled=!playing;
        cancel.Enabled=false;
        state.Text=packageId==null ? "Extract the complete application ZIP before running it." : playing ? "Game running. Close the game to change setup or import saves." : ready ? "Ready to play." : "Select your CUE or CHD image and click Prepare game.";
    }
    void SetBusy(bool value) {
        busy=value;gameChoice.Enabled=!value;prepare.Enabled=play.Enabled=controller.Enabled=import.Enabled=graphics.Enabled=originalGraphics.Enabled=selectDisc.Enabled=selectWork.Enabled=cue.Enabled=work.Enabled=!value;
        cancel.Enabled=value;progress.Style=value ? ProgressBarStyle.Marquee : ProgressBarStyle.Continuous;progress.Value=0;
        if(!value)RefreshReady();
    }
    void Prepare(object sender,EventArgs e) {
        if(!File.Exists(cue.Text)) {MessageBox.Show(this,"Choose your supported CUE or CHD image first.");return;}
        try {
            work.Text=Path.GetFullPath(work.Text);cue.Text=Path.GetFullPath(cue.Text);SaveSettings();
            cancelled=false;lastError=null;
            string python=Path.Combine(root,"payload","python","python.exe"),script=Path.Combine(root,"payload","app","desktop","prepare.py");
            worker=new Process {StartInfo=new ProcessStartInfo(python,"-X utf8 -B "+Quote(script)+" --cue "+Quote(cue.Text)+" --work "+Quote(work.Text)+" --scenario "+Scenarios[activeGame]) {UseShellExecute=false,CreateNoWindow=true,RedirectStandardOutput=true,RedirectStandardError=true},EnableRaisingEvents=true};
            worker.OutputDataReceived+=delegate(object s,DataReceivedEventArgs output) {
                if(output.Data==null || !output.Data.StartsWith("SF3_STATUS "))return;
                try {var data=json.Deserialize<Dictionary<string,object>>(output.Data.Substring(11));Ui(delegate {
                    state.Text=(string)data["message"];if((string)data["phase"]=="error")lastError=state.Text;
                    int total=Convert.ToInt32(data["total"]),done=Convert.ToInt32(data["done"]);
                    if(total>0) {progress.Style=ProgressBarStyle.Continuous;progress.Maximum=total;progress.Value=Math.Min(total,done);}
                });} catch { }
            };
            worker.ErrorDataReceived+=delegate(object s,DataReceivedEventArgs output) {if(output.Data!=null)Ui(delegate {state.Text="Preparation stopped. See the setup log in your game-data folder.";});};
            worker.Exited+=delegate {worker.WaitForExit();int code=worker.ExitCode;Ui(delegate {
                SetBusy(false);if(cancelled)state.Text="Preparation cancelled. Your saves were kept; you can retry.";
                else if(code!=0) {state.Text="Preparation did not finish. Your saves were kept.";MessageBox.Show(this,lastError ?? "Preparation did not finish. Check available disk space and the setup log under preparation in your game-data folder.","Preparation stopped");}
            });};
            SetBusy(true);state.Text="Starting preparation...";worker.Start();worker.BeginOutputReadLine();worker.BeginErrorReadLine();
        } catch(Exception error) {SetBusy(false);MessageBox.Show(this,error.Message,"Could not start preparation");}
    }
    void CancelWorker() {
        if(worker==null)return;
        try {if(!worker.HasExited) {
            cancelled=true;
            var start=new ProcessStartInfo(Path.Combine(Environment.SystemDirectory,"taskkill.exe"),"/PID "+worker.Id+" /T /F") {UseShellExecute=false,CreateNoWindow=true};
            using(var killer=Process.Start(start)) killer.WaitForExit();
        }} catch { }
    }
    void Play(object sender,EventArgs e) {
        try {
            if(!Ready()) {RefreshReady();return;}
            SaveSettings();File.WriteAllText(Path.Combine(work.Text,"save-import-python.txt"),Path.Combine(root,"payload","python","python.exe"));
            var start=ScriptStart("run-native.ps1",false);
            if(smokePlayRoot!=null)start.Arguments+=" -Headless -VBlanks 1800";
            start.RedirectStandardOutput=true;start.RedirectStandardError=true;
            var launchLog=new StreamWriter(Path.Combine(work.Text,"desktop-launch.log"),false,Encoding.UTF8) {AutoFlush=true};
            launchLog.WriteLine(start.FileName+" "+start.Arguments);
            game=new Process {StartInfo=start,EnableRaisingEvents=true};
            game.OutputDataReceived+=delegate(object s,DataReceivedEventArgs output) {if(output.Data!=null)lock(launchLog)launchLog.WriteLine(output.Data);};
            game.ErrorDataReceived+=delegate(object s,DataReceivedEventArgs output) {if(output.Data!=null)lock(launchLog)launchLog.WriteLine(output.Data);};
            game.Exited+=delegate {game.WaitForExit();int code=game.ExitCode;launchLog.Dispose();Ui(delegate {
                game.Dispose();game=null;RefreshReady();
                if(code!=0)state.Text="Game stopped with an error. See desktop-launch.log in your data folder.";
                if(smokePlayRoot!=null) {File.WriteAllText(Path.Combine(smokePlayRoot,code==0 ? "play-passed.txt" : "play-failed.txt"),"Play launcher exit: "+code);Environment.ExitCode=code;Close();}
            });};game.Start();game.BeginOutputReadLine();game.BeginErrorReadLine();RefreshReady();
        } catch(Exception error) {game=null;RefreshReady();MessageBox.Show(this,error.Message,"Could not launch game");}
    }
    ProcessStartInfo ScriptStart(string name,bool sta) {
        string powershell=Path.Combine(Environment.SystemDirectory,"WindowsPowerShell","v1.0","powershell.exe");
        var start=new ProcessStartInfo(powershell,"-NoProfile -ExecutionPolicy Bypass "+(sta ? "-STA " : "")+"-File "+Quote(Path.Combine(work.Text,name))+" -Work "+Quote(work.Text)) {UseShellExecute=false,CreateNoWindow=true,WorkingDirectory=work.Text};
        start.EnvironmentVariables["PSModulePath"]=Path.Combine(Environment.SystemDirectory,"WindowsPowerShell","v1.0","Modules");
        return start;
    }
    void LaunchScript(string name,bool sta) {try {Process.Start(ScriptStart(name,sta));} catch(Exception e) {MessageBox.Show(this,e.Message);}}
    ProcessStartInfo ControllerSetupStart() {
        // Setup asks for console input. The wrapper also keeps detection/error
        // messages visible when setup exits before a controller is selected.
        return new ProcessStartInfo(Path.Combine(work.Text,"Controller Setup.cmd")) {UseShellExecute=true,WorkingDirectory=work.Text,WindowStyle=ProcessWindowStyle.Normal};
    }
    void LaunchControllerSetup() {try {Process.Start(ControllerSetupStart());} catch(Exception e) {MessageBox.Show(this,e.Message,"Could not open controller setup");}}
    void SelectGraphics() {
        using(var d=new OpenFileDialog {Filter="Graphics builder report (packs-report.json)|packs-report.json|JSON files (*.json)|*.json",Title="Choose packs-report.json from the optional graphics builder"})
            if(d.ShowDialog(this)==DialogResult.OK)RunGraphicsTool(d.FileName);
    }
    void RunGraphicsTool(string report) {
        if(busy || game!=null || !Ready())return;
        try {
            string python=Path.Combine(root,"payload","python","python.exe");
            string script=Path.Combine(root,"payload","app","desktop","ai_packs.py");
            string args="-X utf8 -B "+Quote(script)+(report==null ? " detach" : " attach --report "+Quote(report)+" --profiles "+Quote(Path.Combine(root,"payload","app","profiles")))+" --game-dir "+Quote(work.Text);
            var start=new ProcessStartInfo(python,args) {UseShellExecute=false,CreateNoWindow=true,RedirectStandardOutput=true,RedirectStandardError=true};
            using(var process=Process.Start(start)) {
                string output=process.StandardOutput.ReadToEnd(),error=process.StandardError.ReadToEnd();process.WaitForExit();
                if(process.ExitCode!=0)throw new InvalidOperationException(String.IsNullOrWhiteSpace(error)?output:error);
            }
            RefreshReady();state.Text=report==null ? "Original artwork selected. Saves are unchanged." : "Optional artwork attached. Choose AI graphics in the game's menu.";
        } catch(Exception error) {MessageBox.Show(this,error.Message,"Optional graphics");}
    }
    [STAThread] static void Main(string[] args) {
        Application.EnableVisualStyles();Application.SetCompatibleTextRenderingDefault(false);
        string testRoot=args.Length==2 && (args[0]=="--self-test" || args[0]=="--self-test-ready") ? Path.GetFullPath(args[1]) : null;
        var form=new Launcher(testRoot);
        if(testRoot!=null && args[0]=="--self-test-ready") {
            try {
                var ready=form.Read(Path.Combine(testRoot,"desktop-ready.json"));
                if(ready.ContainsKey("profile_id")) {
                    int index=form.ProfileGame((string)ready["profile_id"]);
                    if(index>=0) {form.busy=true;form.activeGame=index;form.SelectGame(index);form.busy=false;}
                }
            } catch { }
            form.work.Text=testRoot;form.smokePlayRoot=testRoot;
            form.Shown+=delegate {
                int wanted=form.activeGame;
                for(int key=0;key<Scenarios.Length;++key) {
                    form.activeGame=key;
                    if(form.Ready()!=(key==wanted)) {File.WriteAllText(Path.Combine(testRoot,"play-failed.txt"),"Prepared revision accepted by wrong game entry");Environment.ExitCode=1;form.Close();return;}
                }
                form.activeGame=wanted;
                form.RefreshReady();
                if(!form.Ready() || !form.play.Enabled || !form.controller.Enabled || !form.import.Enabled || !form.graphics.Enabled || form.originalGraphics.Enabled!=File.Exists(Path.Combine(testRoot,"ai-packs.json"))) {File.WriteAllText(Path.Combine(testRoot,"play-failed.txt"),"Prepared controls are not ready");Environment.ExitCode=1;form.Close();return;}
                if(form.gameChoice.SelectedIndex!=Array.IndexOf(DisplayGames,form.activeGame) || !form.gameChoice.Text.Contains("Scenario "+Scenarios[form.activeGame])) {File.WriteAllText(Path.Combine(testRoot,"play-failed.txt"),"Prepared game selection is incorrect");Environment.ExitCode=1;form.Close();return;}
                form.Refresh();form.gameChoice.Refresh();Application.DoEvents();
                using(var image=new Bitmap(form.Width,form.Height)) {form.DrawToBitmap(image,new Rectangle(Point.Empty,form.Size));image.Save(Path.Combine(testRoot,"ready-launcher.png"));}
                form.Play(null,EventArgs.Empty);
            };
            Application.Run(form);return;
        }
        if(testRoot!=null) form.Shown+=delegate {
            try {
                Directory.CreateDirectory(testRoot);form.SetBusy(true);
                if(form.prepare.Enabled || form.play.Enabled || form.graphics.Enabled || form.originalGraphics.Enabled || !form.cancel.Enabled)throw new Exception("Busy controls are invalid");
                form.SetBusy(false);if(form.play.Enabled || form.graphics.Enabled || form.originalGraphics.Enabled)throw new Exception("Unprepared installation must not be playable or accept artwork");
                var controllerStart=form.ControllerSetupStart();
                if(!controllerStart.UseShellExecute || controllerStart.CreateNoWindow || controllerStart.WindowStyle!=ProcessWindowStyle.Normal || controllerStart.FileName!=Path.Combine(form.work.Text,"Controller Setup.cmd") || controllerStart.WorkingDirectory!=form.work.Text)
                    throw new Exception("Controller setup must open the persistent interactive console in this game's folder");
                string firstWork=form.work.Text;form.cue.Text=Path.Combine(testRoot,"first.cue");
                form.SelectGame(1);
                if(form.work.Text==firstWork || form.cue.Text!="")throw new Exception("Game selection must isolate progress");
                form.cue.Text=Path.Combine(testRoot,"second.chd");form.SelectGame(0);
                if(form.work.Text!=firstWork || form.cue.Text!=Path.Combine(testRoot,"first.cue") || form.gameChoice.SelectedIndex!=0 || !form.gameChoice.Text.Contains("Scenario 1"))throw new Exception("Game selection did not remember per-game paths");
                form.SelectGame(2);string thirdWork=form.work.Text;
                if(thirdWork==firstWork || !thirdWork.EndsWith("-Scenario3-v25_1") || form.cue.Text!="")throw new Exception("Scenario 3 needs a separate data folder");
                form.cue.Text=Path.Combine(testRoot,"third.cue");form.SelectGame(1);
                if(form.cue.Text!=Path.Combine(testRoot,"second.chd") || form.work.Text==thirdWork)throw new Exception("Scenario 3 selection overwrote Scenario 2");
                form.SelectGame(2);
                if(form.work.Text!=thirdWork || form.cue.Text!=Path.Combine(testRoot,"third.cue"))throw new Exception("Scenario 3 paths were not restored");
                form.SelectGame(3);string translatedWork=form.work.Text;
                if(translatedWork==firstWork || translatedWork==thirdWork || Scenarios[form.activeGame]!=1)throw new Exception("Translated Scenario 1 must have separate progress and prepare as Scenario 1");
                form.cue.Text=Path.Combine(testRoot,"translated.cue");form.SelectGame(0);
                if(form.work.Text!=firstWork || form.cue.Text!=Path.Combine(testRoot,"first.cue"))throw new Exception("Translated Scenario 1 changed original USA settings");
                form.SelectGame(3);
                if(form.work.Text!=translatedWork || form.cue.Text!=Path.Combine(testRoot,"translated.cue"))throw new Exception("Translated Scenario 1 paths were not restored");
                form.SelectGame(0);
                using(var image=new Bitmap(form.Width,form.Height)) {form.DrawToBitmap(image,new Rectangle(Point.Empty,form.Size));image.Save(Path.Combine(testRoot,"launcher.png"));}
                form.SaveSettings();
                string legacy=Path.Combine(testRoot,"legacy-settings.json"),original=File.ReadAllText(form.settingsPath);
                File.WriteAllText(legacy,original);form.fallbackSettingsPath=legacy;form.settingsPath=Path.Combine(testRoot,"portable-settings.json");
                if((string)form.ReadSavedSettings()["work"]!=firstWork)throw new Exception("Legacy settings migration failed");
                form.work.Text=Path.Combine(testRoot,"remembered-game");form.SaveSettings();
                if(!File.Exists(form.settingsPath) || (string)form.ReadSavedSettings()["work"]!=form.work.Text || File.ReadAllText(legacy)!=original)
                    throw new Exception("Portable settings must retain game paths without rewriting the legacy settings");
                File.WriteAllText(Path.Combine(testRoot,"passed.txt"),"PASS: first-run state, busy/cancel controls, per-game paths, portable settings migration and UI rendering");
            } catch(Exception error) {File.WriteAllText(Path.Combine(testRoot,"failed.txt"),error.ToString());Environment.ExitCode=1;}
            form.Close();
        };
        Application.Run(form);
    }
}
