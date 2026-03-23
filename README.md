# LatexServer sin renderizado interno

Esta versi\'on elimina la dependencia de **MuPDF** dentro de la aplicaci\'on. La ventana sigue usando **GLFW 3 + OpenGL**, pero ahora el flujo termina al generar el `.pdf` y abrirlo con el visor externo del sistema.

## Dependencias

En Ubuntu/Debian puedes instalar lo necesario con:

```bash
sudo apt update
sudo apt install build-essential cmake libglfw3-dev libopengl-dev texlive-latex-base
```

> `xdg-open` suele venir con el entorno de escritorio. Si no est\'a disponible, instala `xdg-utils`.

## Compilar

```bash
cmake -S . -B build
cmake --build build
```

## Ejecutar

```bash
./build/latex_server
```

## Uso

- `B`: compila `assets/sample.tex` con `pdflatex`.
- `O`: abre `build-output/sample.pdf` con el visor PDF predeterminado.
- `Esc`: cierra la app.

## Qu\'e cambi\'o

- Se mantiene la ventana OpenGL/GLFW como contenedor liviano.
- Ya no se renderiza el PDF dentro de la app.
- El resultado final es el archivo `.pdf` generado por `pdflatex`.
