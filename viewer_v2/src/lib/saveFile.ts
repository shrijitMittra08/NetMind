interface DesktopBridge {
  pywebview?: { api?: { save_text?: (filename: string, text: string) => Promise<string | null> } };
}

export async function saveTextFile(text: string, filename: string, type = "text/plain"): Promise<void> {
  const nativeSave = (window as Window & DesktopBridge).pywebview?.api?.save_text;
  if (nativeSave) {
    await nativeSave(filename, text);
    return;
  }
  const url = URL.createObjectURL(new Blob([text], { type }));
  const anchor = document.createElement("a");
  anchor.href = url;
  anchor.download = filename;
  anchor.click();
  URL.revokeObjectURL(url);
}
