// Authored Windows shell integration for the PowerShell-hosted launcher window.
using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.InteropServices;

public static class TandemTaskbarIdentity {
    public const string AppId = "ProjectTandem.Launcher";
    static readonly Guid PropertyFormat = new Guid("9F4C2855-9F79-4B39-A8D0-E1D42DE1D5F3");

    [StructLayout(LayoutKind.Sequential, Pack=4)]
    struct PropertyKey {
        public Guid Format;
        public uint Id;
        public PropertyKey(uint id) { Format=PropertyFormat; Id=id; }
    }
    [StructLayout(LayoutKind.Explicit, Size=24)]
    struct PropVariant {
        [FieldOffset(0)] public ushort Type;
        [FieldOffset(8)] public IntPtr Value;
    }
    [ComImport, Guid("886D8EEB-8CF2-4446-8D02-CDBA1DBDCF99"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface PropertyStore {
        [PreserveSig] int GetCount(out uint count);
        [PreserveSig] int GetAt(uint index, out PropertyKey key);
        [PreserveSig] int GetValue(ref PropertyKey key, out PropVariant value);
        [PreserveSig] int SetValue(ref PropertyKey key, ref PropVariant value);
        [PreserveSig] int Commit();
    }
    [DllImport("shell32.dll", CharSet=CharSet.Unicode)]
    static extern int SetCurrentProcessExplicitAppUserModelID(string appId);
    [DllImport("shell32.dll")]
    static extern int GetCurrentProcessExplicitAppUserModelID(out IntPtr appId);
    [DllImport("shell32.dll")]
    static extern int SHGetPropertyStoreForWindow(IntPtr window, ref Guid iid, out PropertyStore store);
    [DllImport("ole32.dll")]
    static extern int PropVariantClear(ref PropVariant value);
    [DllImport("user32.dll")]
    static extern IntPtr SendMessage(IntPtr window, uint message, IntPtr wParam, IntPtr lParam);

    static PropertyStore Store(IntPtr window) {
        var iid=typeof(PropertyStore).GUID;
        PropertyStore store;
        Marshal.ThrowExceptionForHR(SHGetPropertyStoreForWindow(window, ref iid, out store));
        return store;
    }
    static void Set(PropertyStore store, uint id, string text) {
        var key=new PropertyKey(id);
        var value=new PropVariant { Type=31, Value=Marshal.StringToCoTaskMemUni(text) };
        try { Marshal.ThrowExceptionForHR(store.SetValue(ref key, ref value)); }
        finally { PropVariantClear(ref value); }
    }
    static string Get(PropertyStore store, uint id) {
        var key=new PropertyKey(id);
        PropVariant value;
        Marshal.ThrowExceptionForHR(store.GetValue(ref key, out value));
        try { return value.Type==31 ? Marshal.PtrToStringUni(value.Value) : null; }
        finally { PropVariantClear(ref value); }
    }
    public static void InitializeProcess() {
        Marshal.ThrowExceptionForHR(SetCurrentProcessExplicitAppUserModelID(AppId));
    }
    public static void Apply(IntPtr window, string root, string iconPath, IntPtr icon) {
        var store=Store(window);
        try {
            // Relaunch properties precede the explicit window identity. Pinned
            // shortcuts must reopen Tandem rather than its PowerShell host.
            Set(store, 2, "\""+Path.Combine(root,"ProjectTandem.exe")+"\"");
            Set(store, 4, "Project Tandem");
            Set(store, 3, iconPath+",0");
            Set(store, 5, AppId);
            Marshal.ThrowExceptionForHR(store.Commit());
        } finally { Marshal.FinalReleaseComObject(store); }
        SendMessage(window, 0x80, new IntPtr(1), icon); // WM_SETICON / ICON_BIG
        SendMessage(window, 0x80, IntPtr.Zero, icon);  // WM_SETICON / ICON_SMALL
    }
    public static Dictionary<string,string> Inspect(IntPtr window) {
        var result=new Dictionary<string,string>();
        var store=Store(window);
        try {
            result["window_app_id"]=Get(store,5);
            result["relaunch_command"]=Get(store,2);
            result["relaunch_name"]=Get(store,4);
            result["icon_resource"]=Get(store,3);
        } finally { Marshal.FinalReleaseComObject(store); }
        IntPtr appId;
        Marshal.ThrowExceptionForHR(GetCurrentProcessExplicitAppUserModelID(out appId));
        try { result["process_app_id"]=Marshal.PtrToStringUni(appId); }
        finally { Marshal.FreeCoTaskMem(appId); }
        result["large_icon_handle"]=SendMessage(window,0x7F,new IntPtr(1),IntPtr.Zero).ToInt64().ToString();
        result["small_icon_handle"]=SendMessage(window,0x7F,IntPtr.Zero,IntPtr.Zero).ToInt64().ToString();
        return result;
    }
}
