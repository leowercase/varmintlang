// https://emscripten.org/docs/tools_reference/emcc.html#emcc-pre-js

const { AnsiUp } = await import("/ansi_up/ansi_up.js", { with: { type: "js" } });
{
  const ansi_up = new AnsiUp();

  ansi_up.use_classes = true;

  const stdout = document.getElementById("stdout");
  const print = text =>
    stdout.insertAdjacentHTML("beforeend", ansi_up.ansi_to_html(text + "\n"));

  Module["print"] = Module["printErr"] = print;
}
