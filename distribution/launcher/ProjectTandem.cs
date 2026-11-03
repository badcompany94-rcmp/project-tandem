// Authored launcher bootstrap; does not contain any recompiled game code.
using System;
using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Windows.Forms;
class ProjectTandem {
    static string Quote(string value) {
        return "\"" + System.Text.RegularExpressions.Regex.Replace(
            System.Text.RegularExpressions.Regex.Replace(value, "(\\\\*)\"", "$1$1\\\""), "(\\\\+)$", "$1$1") + "\"";
    }
    static bool Installed(string root) {
        return Directory.Exists(root) && File.Exists(Path.Combine(root,"Tandem-Launcher.ps1")) && File.Exists(Path.Combine(root,"package-manifest.json"));
    }
    static string ResolveRoot(string ownRoot,string registration) {
        if(Installed(ownRoot))return ownRoot;
        try {
            if(File.Exists(registration) && new FileInfo(registration).Length<=32768) {
                string remembered=File.ReadAllText(registration).Trim();
                if(Installed(remembered))return remembered;
            }
        }catch(IOException){}catch(UnauthorizedAccessException){}
        return ownRoot;
    }
    [STAThread] static int Main(string[] args) {
        string temporary=null;
        try {
            string root=AppDomain.CurrentDomain.BaseDirectory;
            bool installerTest=args.Length>=1 && (args[0]=="--installer-self-test" || args[0]=="--installer-integration-test");
            if(args.Length!=0 && !(args.Length==2 && args[0]=="--self-test") &&
                !(args.Length==2 && args[0]=="--installer-self-test") &&
                !(args.Length==3 && args[0]=="--installer-integration-test"))throw new ArgumentException("Unknown launcher arguments.");
            string registration=Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),"ProjectTandem\\installed-root.txt");
            if(!installerTest)root=ResolveRoot(root,registration);
            bool recovery=File.Exists(Path.Combine(root,"local-data/update-transaction.json")) && args.Length==0;
            string script=recovery?Path.Combine(root,"local-data/updates/active/Update-Tandem.ps1"):Path.Combine(root,"Tandem-Launcher.ps1");
            bool install=installerTest || !Installed(root);
            if(install) {
                temporary=Path.Combine(Path.GetTempPath(),"TandemInstaller-"+Guid.NewGuid().ToString("N"));
                Directory.CreateDirectory(temporary);
                var assembly=Assembly.GetExecutingAssembly();
                foreach(string resource in assembly.GetManifestResourceNames()) {
                    if(!resource.StartsWith("TandemInstaller."))continue;
                    string name=resource.Substring("TandemInstaller.".Length);
                    if(Path.GetFileName(name)!=name)throw new Exception("Invalid installer resource.");
                    using(var input=assembly.GetManifestResourceStream(resource))
                    using(var output=File.Create(Path.Combine(temporary,name)))input.CopyTo(output);
                }
                root=temporary;script=Path.Combine(temporary,"Install-Tandem.ps1");recovery=false;
                if(!File.Exists(script))throw new FileNotFoundException("This launcher is missing its installer. Download the current ProjectTandem.exe from the official GitHub release.");
            }
            var info=new ProcessStartInfo(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.System),"WindowsPowerShell\\v1.0\\powershell.exe"));
            info.Arguments="-NoProfile -STA -ExecutionPolicy Bypass -File "+Quote(script);
            if(install) {
                info.Arguments+=" -Bootstrap "+Quote(Assembly.GetExecutingAssembly().Location);
                if(installerTest)info.Arguments+=" -Evidence "+Quote(Path.GetFullPath(args[1]))+
                    (args[0]=="--installer-self-test"?" -SelfTest":" -IntegrationTest -TestImage "+Quote(Path.GetFullPath(args[2])));
            }
            if(recovery)info.Arguments+=" -Root "+Quote(root)+" -Recover -WaitId "+Process.GetCurrentProcess().Id+" -WaitStarted "+Quote(Process.GetCurrentProcess().StartTime.ToUniversalTime().ToString("o"));
            if(args.Length==2 && args[0]=="--self-test")info.Arguments+=" -SelfTest -Evidence "+Quote(args[1]);
            info.WorkingDirectory=root;info.UseShellExecute=false;info.CreateNoWindow=true;
            info.EnvironmentVariables.Remove("PSMODULEPATH");
            using(var process=Process.Start(info)){if(!recovery){process.WaitForExit();if(process.ExitCode!=0)throw new Exception("The launcher could not start. Use Launch-Tandem.cmd or check the extracted package.");}}
            return 0;
        }catch(Exception error){
            if(args.Length!=0)Console.Error.WriteLine(error.Message);
            else MessageBox.Show(error.Message,"Project Tandem",MessageBoxButtons.OK,MessageBoxIcon.Error);
            return 1;
        }
        finally {
            // Only the fresh, uniquely owned extraction folder may be removed.
            if(temporary!=null)try{
                string expected=Path.GetFullPath(Path.GetTempPath()).TrimEnd(Path.DirectorySeparatorChar)+Path.DirectorySeparatorChar;
                string target=Path.GetFullPath(temporary);
                if(target.StartsWith(expected,StringComparison.OrdinalIgnoreCase) &&
                    Path.GetFileName(target).StartsWith("TandemInstaller-",StringComparison.Ordinal) &&
                    (File.GetAttributes(target)&FileAttributes.ReparsePoint)==0)Directory.Delete(target,true);
            }catch{}
        }
    }
}
