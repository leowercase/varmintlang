// https://emscripten.org/docs/tools_reference/emcc.html#emcc-pre-js
{
  const stdout = document.getElementById("stdout");
  const print =
    text => stdout.insertAdjacentHTML("beforeend", text + "\n");

  Module["print"] = Module["printErr"] = print;
}
