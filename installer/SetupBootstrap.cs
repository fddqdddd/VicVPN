using System;
using System.Diagnostics;
using System.IO;
using System.IO.Compression;
using System.Reflection;
using System.Windows.Forms;

static class VicVpnSetup
{
    [STAThread]
    static int Main()
    {
        var extractDir = Path.Combine(Path.GetTempPath(), "VicVPN-setup-" + Guid.NewGuid().ToString("N"));
        try
        {
            Directory.CreateDirectory(extractDir);
            using (var stream = Assembly.GetExecutingAssembly().GetManifestResourceStream("payload"))
            {
                if (stream == null)
                {
                    MessageBox.Show(
                        "Installer payload not found.\nRebuild VicVPN setup EXE.",
                        "VicVPN", MessageBoxButtons.OK, MessageBoxIcon.Error);
                    return 1;
                }
                using (var zip = new ZipArchive(stream, ZipArchiveMode.Read))
                    zip.ExtractToDirectory(extractDir);
            }

            var installScript = Path.Combine(extractDir, "Install-VicVPN.ps1");
            if (!File.Exists(installScript))
            {
                MessageBox.Show(
                    "Install-VicVPN.ps1 missing in archive.",
                    "VicVPN", MessageBoxButtons.OK, MessageBoxIcon.Error);
                return 1;
            }

            var psi = new ProcessStartInfo
            {
                FileName = "powershell.exe",
                Arguments = "-NoProfile -ExecutionPolicy Bypass -File \"" + installScript +
                            "\" -Silent -DesktopIcon",
                UseShellExecute = true,
                Verb = "runas",
                WindowStyle = ProcessWindowStyle.Hidden
            };

            using (var proc = Process.Start(psi))
            {
                if (proc == null)
                {
                    MessageBox.Show(
                        "Installation cancelled (admin rights required).",
                        "VicVPN", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                    return 1;
                }
                proc.WaitForExit();
                if (proc.ExitCode != 0)
                {
                    MessageBox.Show(
                        "Installation failed (code " + proc.ExitCode + ").",
                        "VicVPN", MessageBoxButtons.OK, MessageBoxIcon.Error);
                    return proc.ExitCode;
                }
            }

            var installDir = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles), "VicVPN");
            if (!File.Exists(Path.Combine(installDir, "VicVPN.exe")))
            {
                MessageBox.Show(
                    "VicVPN.exe was not found after install.\nExpected: " + installDir,
                    "VicVPN", MessageBoxButtons.OK, MessageBoxIcon.Error);
                return 1;
            }

            MessageBox.Show(
                "VicVPN installed successfully.\n\n" +
                "Run VicVPN as Administrator for VPN (TUN).",
                "VicVPN", MessageBoxButtons.OK, MessageBoxIcon.Information);
            return 0;
        }
        catch (Exception ex)
        {
            MessageBox.Show("Installation error:\n" + ex.Message, "VicVPN",
                MessageBoxButtons.OK, MessageBoxIcon.Error);
            return 1;
        }
        finally
        {
            try
            {
                if (Directory.Exists(extractDir))
                    Directory.Delete(extractDir, true);
            }
            catch
            {
            }
        }
    }
}
