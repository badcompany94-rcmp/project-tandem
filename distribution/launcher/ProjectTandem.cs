// Authored launcher bootstrap; does not contain any recompiled game code.
using System;
using System.Diagnostics;
using System.IO;
using System.Windows.Forms;
class ProjectTandem {
    static string Quote(string value) {
        return "\"" + System.Text.RegularExpressions.Regex.Replace(
            System.Text.RegularExpressions.Regex.Replace(value, "(\\\\*)\"", "$1$1\\\""), "(\\\\+)$", "$1$1") + "\"";
    }
    [STAThread] static int Main(string[] args) {
        try {
            string root=AppDomain.CurrentDomain.BaseDirectory;
            bool recovery=File.Exists(Path.Combine(root,"local-data/update-transaction.json")) && args.Length==0;
            string script=recovery?Path.Combine(root,"local-data/updates/active/Update-Tandem.ps1"):Path.Combine(root,"Tandem-Launcher.ps1");
            if(!File.Exists(script))throw new FileNotFoundException("Extract the complete Project Tandem download before opening the launcher.");
            var info=new ProcessStartInfo(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.System),"WindowsPowerShell\\v1.0\\powershell.exe"));
            info.Arguments="-NoProfile -STA -ExecutionPolicy Bypass -File "+Quote(script);
            if(recovery)info.Arguments+=" -Root "+Quote(root)+" -Recover -WaitId "+Process.GetCurrentProcess().Id+" -WaitStarted "+Quote(Process.GetCurrentProcess().StartTime.ToUniversalTime().ToString("o"));
            if(args.Length==2 && args[0]=="--self-test")info.Arguments+=" -SelfTest -Evidence "+Quote(args[1]);
            else if(args.Length!=0)throw new ArgumentException("Unknown launcher arguments.");
            info.WorkingDirectory=root;info.UseShellExecute=false;info.CreateNoWindow=true;
            info.EnvironmentVariables.Remove("PSMODULEPATH");
            using(var process=Process.Start(info)){if(!recovery){process.WaitForExit();if(process.ExitCode!=0)throw new Exception("The launcher could not start. Use Launch-Tandem.cmd or check the extracted package.");}}
            return 0;
        }catch(Exception error){MessageBox.Show(error.Message,"Project Tandem",MessageBoxButtons.OK,MessageBoxIcon.Error);return 1;}
    }
}
