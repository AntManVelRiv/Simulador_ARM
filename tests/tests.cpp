// tests/tests.cpp — pruebas del MODELO y del CONTROLADOR (sin interfaz gráfica).
#include "model/SimModel.h"
#include "model/Examples.h"
#include "controller/SimController.h"
#include "model/Runner.h"
#include <sstream>
#include <iostream>
#include <functional>
#include <filesystem>
using namespace arm;

static int failures = 0, total = 0;
#define CHECK(cond) do { total++; if (!(cond)) { failures++; std::cerr << "FALLO " << __FILE__ << ":" << __LINE__ << "  " #cond "\n"; } } while (0)

static Machine runSrc(const std::string& src, Program& holder) {
    holder = assemble(src); Machine m(holder); m.run(200000); return m;
}

int main() {
    // --- Codificación: coincide con el ejemplo resuelto del tema (PDF) ---
    CHECK(encodeLine("ADD R1, R2, R3", 0x1000, {}).word == 0xE0821003);
    CHECK(encodeLine("LDR R5, [R0, R3, LSL #2]", 0x1004, {}).word == 0xE7905103);
    CHECK(encodeLine("MOV R1, R2, LSL #2", 0x1008, {}).word == 0xE1A01102);
    CHECK(encodeLine("LDR R5, [R0, #1]", 0x100C, {}).word == 0xE5905001);
    CHECK(encodeLine("BLT label", 0x1010, {{"label", 0x1000}}).word == 0xBAFFFFFA);
    CHECK(!encodeLine("FOO R1", 0, {}).ok);
    CHECK(!encodeLine("MOV R1, #0x12345", 0, {}).ok);   // inmediato no codificable

    // --- Ejecución ---
    { Program p; auto m = runSrc("MOV R2,#10\nMOV R3,#7\nMOV R4,#3\nADD R1,R2,R3\nSUB R1,R1,R4\n", p); CHECK(m.r[1] == 14); }
    { Program p; auto m = runSrc("LDR R7,=0x0FAFF0C3\nLDR R3,=0xB4D6B268\nAND R1,R7,R3\nMVN R8,R7\nMOV R4,#9\nASR R6,R3,R4\n", p);
      CHECK(m.r[1] == 0x0486B040); CHECK(m.r[8] == 0xF0500F3C); CHECK(m.r[6] == 0xFFDA6B59); }
    { Program p; auto m = runSrc("MOV R3,#9\nMOV R4,#9\nCMP R3,R4\nADDEQ R1,R4,R3\nCMP R3,#1\nADDEQ R2,R4,R3\n", p); CHECK(m.r[1] == 18); CHECK(m.r[2] == 0); }
    { Program p; auto m = runSrc(".data\narray: .word 1,2,3,4,5,6\n.text\nLDR R0,=array\nADD R1,R0,#24\nL CMP R0,R1\nBGE F\nLDR R2,[R0]\nMOV R2,R2,LSL #3\nSTR R2,[R0],#4\nB L\nF\n", p);
      uint32_t base = p.symbols.at("array"); for (int i = 0; i < 6; i++) CHECK(m.readWord(base + 4 * i) == (uint32_t)(8 * (i + 1))); }
    { Program p; auto m = runSrc(".data\nb: .byte 0xF0\n.text\nLDR R4,=b\nLDRB R5,[R4]\nLDRSB R6,[R4]\n", p); CHECK(m.r[5] == 0xF0); CHECK(m.r[6] == 0xFFFFFFF0); }
    { Program p; auto m = runSrc("LDR R0,=0x80000000\nMOV R1,#2\nSMULL R2,R3,R0,R1\n", p); CHECK(m.r[2] == 0); CHECK(m.r[3] == 0xFFFFFFFF); }
    { Program p; auto m = runSrc("main: MOV R0,#5\nBL F\nMOV R4,R0\nSWI 0x11\nF PUSH {R0,LR}\nCMP R0,#1\nBGT E\nMOV R0,#1\nADD SP,SP,#8\nMOV PC,LR\nE SUB R0,R0,#1\nBL F\nPOP {R1,LR}\nMUL R0,R1,R0\nMOV PC,LR\n", p);
      CHECK(m.r[4] == 120); CHECK(m.haltKind == "exit"); CHECK(m.r[13] == STACK_TOP); }
    { AsmOptions o; o.dataBase = 0x5000; auto p = assemble(".data\nv: .word 7\n.text\nLDR R0,=v\nLDR R1,[R0]\n", o); Machine m(p); m.run(100); CHECK(p.symbols.at("v") == 0x5000); CHECK(m.r[1] == 7); }
    { auto p = assemble("MOV R0, #1\nBADINSTR R1\n"); CHECK(!p.ok); CHECK(p.errors.size() == 1 && p.errors[0].line == 2); }
    { auto p = assemble("B nowhere\n"); CHECK(!p.ok); }

    // --- Paso atrás ---
    { auto p = assemble(".data\nv: .word 1\n.text\nLDR R0,=v\nMOV R1,#5\nSTR R1,[R0]\n"); Machine m(p); m.step(); m.step(); m.step();
      CHECK(m.readWord(p.symbols.at("v")) == 5); CHECK(m.back()); CHECK(m.readWord(p.symbols.at("v")) == 1); CHECK(m.back()); CHECK(m.r[1] == 0); }

    // --- Entrada / salida de consola ---
    { auto p = assemble("MOV R0,#65\nSWI 0x20\nMVN R0,#6\nSWI 0x21\nMOV R0,#255\nSWI 0x23\nSWI 0x25\nSWI 0x11\n");
      Machine m(p); m.run(1000); CHECK(m.output == "A-70x000000FF\n"); CHECK(m.haltKind == "exit"); }
    { auto p = assemble(".data\ns: .asciz \"hola\"\n.text\nLDR R0,=s\nSWI 0x24\nSWI 0x11\n"); Machine m(p); m.run(1000); CHECK(m.output == "hola"); }
    { // lectura bloqueante: SWI 0x30 (entero) debe detener el programa hasta recibir una linea
      auto p = assemble("SWI 0x30\nMOV R4,R0\nSWI 0x11\n"); Machine m(p);
      auto r1 = m.step(); CHECK(!r1.has_value()); CHECK(m.halted); CHECK(m.haltKind == "input");
      m.feedInput("42\n"); CHECK(!m.halted);
      auto r2 = m.step(); CHECK(r2.has_value()); m.run(100); CHECK(m.r[4] == 42); CHECK(m.haltKind == "exit"); }
    { // lectura de caracter y de cadena
      auto p = assemble(".data\nbuf: .space 16\n.text\nSWI 0x31\nMOV R4,R0\nLDR R1,=buf\nMOV R2,#16\nSWI 0x32\nMOV R5,R0\nSWI 0x11\n");
      Machine m(p); m.step(); CHECK(m.halted && m.haltKind == "input");
      m.feedInput("Zbc\n"); CHECK(!m.halted);
      m.run(100); CHECK(m.r[4] == 'Z'); CHECK(m.r[5] == 2);   // "Z" ya se leyo con SWI 0x31; queda "bc"
      uint32_t buf = p.symbols.at("buf"); CHECK(m.readByte(buf) == 'b'); CHECK(m.readByte(buf+1) == 'c'); CHECK(m.readByte(buf+2) == 0); }

    // --- Macros (.macro / .endm), estilo GNU as con sustitucion \param ---
    { // la macro "reducir" del ejercicio de la Relacion 2 (UAL)
      auto p = assemble(".macro reducir a,b,c\nadd \\a,\\a,\\b\nadd \\a,\\a,\\c\n.endm\n\n.data\nx: .word 0\n.text\n.global main\nmain: push {r10,lr}\nmov r0, #3\nmov r1, #4\nmov r2, #5\nreducir r0,r1,r2\nldr r3,=x\nstr r0,[r3]\npop {r10,lr}\nbx lr\n");
      CHECK(p.ok); if (p.ok) { Machine m(p); m.run(1000); CHECK(m.r[0] == 12); CHECK(m.readWord(p.symbols.at("x")) == 12); } }
    { auto p = assemble(".macro foo a,b\nadd \\a,\\a,\\b\n.endm\nfoo r0\n"); CHECK(!p.ok); }  // num. de argumentos incorrecto
    { // "\@" da un identificador unico por invocacion: la misma macro dos veces no choca de etiquetas
      auto p = assemble(".macro incr r\ncmp \\r,#0\nbeq skip\\@\nadd \\r,\\r,#1\nskip\\@:\n.endm\nmov r0,#5\nmov r1,#0\nincr r0\nincr r1\n");
      CHECK(p.ok); if (p.ok) { Machine m(p); m.run(1000); CHECK(m.r[0] == 6); CHECK(m.r[1] == 0); } }

    // --- Funciones de biblioteca simuladas por "BL": printf, scanf, puts, gets, strlen ---
    { // el ejemplo exacto de "capital" con printf("%s") de la Relacion 2 (UAL)
      auto p = assemble("main: push {r2,lr}\nldr r0, =msg1\nldr r1,=array\nbl printf\nldr r0, =array\nmov r1, #15\nbl Capital\nldr r0, =msg2\nldr r1, =array\nbl printf\npop {r2,lr}\nbx lr\n"
                         "Capital: push {r4,r5,r6,lr}\nmov r4,#0\nbucle: cmp r4, r1\nbgt finbucle\nldrb r5, [r0, r4]\ncmp r5, #97\nblt conti\ncmp r5, #122\nbgt conti\nsub r5, r5, #32\nstrb r5, [r0, r4]\nconti: add r4,r4,#1\nb bucle\nfinbucle: pop {r4,r5,r6,lr}\nbx lr\n"
                         ".data\narray: .asciz \"abc\"\nmsg1: .asciz \"Antes: %s\\n\"\nmsg2: .asciz \"Despues: %s\\n\"\n");
      CHECK(p.ok); if (p.ok) { Machine m(p); m.run(10000); CHECK(m.output == "Antes: abc\nDespues: ABC\n"); } }
    { auto p = assemble("ldr r0,=fmt\nmov r1,#7\nmov r2,#9\nbl printf\n.data\nfmt: .asciz \"%d+%d\"\n");
      CHECK(p.ok); if (p.ok) { Machine m(p); m.run(1000); CHECK(m.output == "7+9"); } }
    { // scanf bloquea hasta tener una linea de entrada, igual que SWI 0x30
      auto p = assemble(".data\nfmt: .asciz \"%d\"\nv: .word 0\n.text\nldr r0,=fmt\nldr r1,=v\nbl scanf\n");
      Machine m(p); m.run(100); CHECK(m.halted); CHECK(m.haltKind == "input");
      m.feedInput("123\n"); CHECK(!m.halted); m.run(100); CHECK(m.readWord(p.symbols.at("v")) == 123); }
    { auto p = assemble("ldr r0,=s\nbl puts\nldr r0,=s\nbl strlen\nmov r4,r0\n.data\ns: .asciz \"hola\"\n");
      CHECK(p.ok); if (p.ok) { Machine m(p); m.run(1000); CHECK(m.output == "hola\n"); CHECK(m.r[4] == 4); } }
    { auto p = assemble(".data\nbuf: .space 16\n.text\nldr r0,=buf\nbl gets\n");
      Machine m(p); m.run(100); CHECK(m.halted && m.haltKind == "input");
      m.feedInput("mundo\n"); m.run(100);
      uint32_t b = p.symbols.at("buf"); CHECK(m.readByte(b) == 'm'); CHECK(m.readByte(b + 4) == 'o'); CHECK(m.readByte(b + 5) == 0); }


    // --- MUL/MLA/SMULL: Rd puede coincidir con Rm, Rs o Rn (antes daba error) ---
    { Program p; auto m = runSrc("MOV R1,#6\nMOV R2,#7\nMUL R1,R1,R2\nMOV R3,#5\nMLA R3,R3,R2,R3\nMOV R4,#3\nMUL R4,R2,R4\nLDR R6,=0x80000000\nMOV R7,#2\nSMULL R6,R8,R6,R7\n", p);
      CHECK(p.ok); CHECK(m.r[1] == 42); CHECK(m.r[3] == 40); CHECK(m.r[4] == 21); CHECK(m.r[6] == 0); CHECK(m.r[8] == 0xFFFFFFFF); }
    { auto p = assemble("MUL R0,R0,R0\n"); CHECK(p.ok); }
    { auto p = assemble("UMULL R1,R1,R2,R3\n"); CHECK(!p.ok); }      // RdLo == RdHi sigue siendo impredecible
    { auto p = assemble("MUL PC,R1,R2\n"); CHECK(!p.ok); }           // PC no se admite como operando
    { auto e = encodeLine("MUL R1, R1, R2", 0x1000, {}); CHECK(e.ok); }

    // --- Directivas de GNU as: .align real en datos, .section, .hword, .comm, ejecucion desde main ---
    { auto p = assemble(".section .data\ns: .asciz \"abc\"\n.align 2\nw: .word 7\n.balign 8\nz: .word 1\n.text\n.global main\nmain: bx lr\n"); CHECK(p.ok);
      if (p.ok) { CHECK(p.symbols.at("w") % 4 == 0); CHECK(p.symbols.at("z") % 8 == 0); } }
    { auto p = assemble(".data\nh: .hword 0x1234, 0x5678\n.text\nmain: ldr r0,=h\nldrh r1,[r0]\nldrh r2,[r0,#2]\n"); CHECK(p.ok); if (p.ok) { Machine m(p); m.run(100); CHECK(m.r[1] == 0x1234); CHECK(m.r[2] == 0x5678); } }
    { auto p = assemble(".comm buf, 16, 4\n.text\nmain: ldr r0,=buf\n"); CHECK(p.ok); if (p.ok) CHECK(p.symbols.at("buf") % 4 == 0); }
    { Program p; auto m = runSrc("helper: mov r5,#99\nbx lr\nmain: mov r4,#1\nbl helper\nswi 0x11\n", p); CHECK(m.r[4] == 1); CHECK(m.r[5] == 99); }   // empieza en main, no en la 1.ª instruccion

    // --- printf: anchura, relleno y mas conversiones ---
    { auto p = assemble("main: push {lr}\nldr r0,=f\nmov r1,#42\nmov r2,#255\nldr r3,=s\nbl printf\npop {lr}\nbx lr\n.data\nf: .asciz \"[%5d][%03d][%-4s]\\n\"\ns: .asciz \"ab\"\n");
      CHECK(p.ok); if (p.ok) { Machine m(p); m.run(1000); CHECK(m.output == "[   42][255][ab  ]\n"); } }
    { // el 4.º argumento de printf va en la pila (AAPCS)
      auto p = assemble("main: push {lr}\nmov r4,#4\npush {r4}\nldr r0,=f\nmov r1,#1\nmov r2,#2\nmov r3,#3\nbl printf\nadd sp,sp,#4\npop {lr}\nbx lr\n.data\nf: .asciz \"%d%d%d%d\"\n");
      CHECK(p.ok); if (p.ok) { Machine m(p); m.run(1000); CHECK(m.output == "1234"); } }
    { auto p = assemble("main: push {lr}\nldr r0,=f\nmov r1,#42\nmov r2,#255\nbl printf\npop {lr}\nbx lr\n.data\nf: .asciz \"[%5d][%03d]\\n\"\n"); CHECK(p.ok); if (p.ok) { Machine m(p); m.run(1000); CHECK(m.output == "[   42][255]\n"); } }
    { auto p = assemble("main: push {lr}\nmov r0,#72\nbl putchar\nmov r0,#105\nbl putchar\npop {lr}\nbx lr\n"); CHECK(p.ok); if (p.ok) { Machine m(p); m.run(1000); CHECK(m.output == "Hi"); } }

    // --- Modo sin interfaz (Runner): equivale a ejecutar el binario o a llamar desde C a una rutina ensamblador ---
    { auto p = assemble("        .global main\nmain:   push {lr}\n        ldr r0,=f\n        mov r1,#6\n        bl printf\n        pop {lr}\n        mov r0,#0\n        bx lr\n        .data\nf:      .asciz \"Valor=%d\\n\"\n");
      RunOptions ro; std::istringstream in; std::ostringstream out, diag; RunResult r = runProgram(p, ro, in, out, diag);
      CHECK(r.ok); CHECK(out.str() == "Valor=6\n"); CHECK(r.haltKind == "exit"); }
    { // llamada estilo C: suma(a, b) devuelve R0 = a + b
      auto p = assemble("suma: add r0,r0,r1\nbx lr\n"); RunOptions ro; ro.callStyle = true; ro.entry = "suma";
      CallArg a, b; std::string err; CHECK(parseCallArg("20", a, err)); CHECK(parseCallArg("22", b, err)); ro.args = {a, b};
      std::istringstream in; std::ostringstream out, diag; RunResult r = runProgram(p, ro, in, out, diag); CHECK(r.ok); CHECK(r.r0 == 42); }
    { // cadenas y arrays como argumentos (punteros): longitud de cadena y suma de un array
      auto p = assemble("largo: mov r2,#0\nl1: ldrb r3,[r0,r2]\ncmp r3,#0\naddne r2,r2,#1\nbne l1\nmov r0,r2\nbx lr\n"
                        "sumar: mov r2,#0\nmov r3,#0\nl2: cmp r3,r1\nbge f2\nldr r12,[r0,r3,lsl #2]\nadd r2,r2,r12\nadd r3,r3,#1\nb l2\nf2: mov r0,r2\nbx lr\n");
      CHECK(p.ok);
      { RunOptions ro; ro.callStyle = true; ro.entry = "largo"; CallArg a; std::string e; CHECK(parseCallArg("s:hola mundo", a, e)); ro.args = {a};
        std::istringstream in; std::ostringstream out, diag; RunResult r = runProgram(p, ro, in, out, diag); CHECK(r.ok); CHECK(r.r0 == 10); }
      { RunOptions ro; ro.callStyle = true; ro.entry = "sumar"; CallArg a, n; std::string e; CHECK(parseCallArg("w:1,2,3,4", a, e)); CHECK(parseCallArg("4", n, e)); ro.args = {a, n};
        std::istringstream in; std::ostringstream out, diag; RunResult r = runProgram(p, ro, in, out, diag); CHECK(r.ok); CHECK(r.r0 == 10); } }
    { // scanf con entrada simulada (--input)
      auto p = assemble("main: push {lr}\nldr r0,=f\nldr r1,=v\nbl scanf\nldr r1,=v\nldr r1,[r1]\nldr r0,=g\nbl printf\npop {lr}\nbx lr\n.data\nf: .asciz \"%d\"\ng: .asciz \"leido %d\\n\"\nv: .word 0\n");
      RunOptions ro; ro.useStdinText = true; ro.stdinText = "123\n"; std::istringstream in; std::ostringstream out, diag; RunResult r = runProgram(p, ro, in, out, diag);
      CHECK(r.ok); CHECK(out.str() == "leido 123\n"); }
    { CallArg a; std::string e; CHECK(!parseCallArg("hola", a, e)); CHECK(parseCallArg("-5", a, e) && (int32_t)a.value == -5); CHECK(parseCallArg("'A'", a, e) && a.value == 65); }

    // --- Rutas de archivo y apertura ---
    { CHECK(normalizeUserPath("  \"/tmp/a b.s\"  ") == "/tmp/a b.s"); CHECK(normalizeUserPath("file:///tmp/a%20b.s") == "/tmp/a b.s"); }
    { SimModel model; SimController ctl(model);
      std::string tmp = (std::filesystem::temp_directory_path() / "armsim_open_test.s").string();
      CHECK(ctl.openFile("/no/existe/nada.s") == false);                       // error claro, sin colgarse
      ctl.editorChanged("MOV R0,#7\n"); CHECK(ctl.saveFile("\"" + tmp + "\""));   // ruta con comillas pegadas
      CHECK(ctl.openFile("  " + tmp + "  ")); CHECK(model.source().find("MOV R0,#7") != std::string::npos);
      CHECK(!ctl.openFile(std::filesystem::temp_directory_path().string()));     // una carpeta no es un archivo
      std::filesystem::remove(tmp); }

    // --- Ahorro de energia ---
    { UiPrefs u; setPowerOverride(-1); u.powerMode = 1; CHECK(resolveLowPower(1)); CHECK(!resolveLowPower(2)); setPowerOverride(0); CHECK(!resolveLowPower(1)); setPowerOverride(1); CHECK(resolveLowPower(2)); setPowerOverride(-1);
      SimModel model; Settings s = model.settings(); s.ui.powerMode = 1; model.applySettings(s); CHECK(model.settings().ui.lowPower); CHECK(!model.settings().ui.anim());
      s.ui.powerMode = 2; model.applySettings(s); CHECK(!model.settings().ui.lowPower); }

    // --- Ejemplos incluidos: todos ensamblan y terminan sin error ---
    for (auto& e : examples()) { auto p = assemble(e.code); CHECK(p.ok); if (!p.ok) { std::cerr << "  ejemplo: " << e.name << "\n"; continue; }
        Machine m(p); m.run(100000); CHECK(m.haltKind != "error" && m.haltKind != "limit"); }

    // --- Modelo + controlador (flujo completo sin interfaz) ---
    { SimModel model; SimController ctl(model);
      ctl.loadExample(0); CHECK(model.program().ok); CHECK(model.machine() != nullptr);
      uint64_t rev = model.externalSourceRevision();
      ctl.step(); ctl.step(); CHECK(model.machine()->steps == 2); CHECK(model.lastStep() != nullptr); CHECK(model.trace().size() == 2);
      ctl.stepBack(); CHECK(model.machine()->steps == 1); CHECK(model.trace().size() == 1);
      ctl.reset(); CHECK(model.machine()->steps == 0);
      ctl.editorChanged("MOV R0, #1\n"); CHECK(model.isDirty()); CHECK(model.externalSourceRevision() == rev);  // el editor no se recarga a sí mismo
      ctl.step(); CHECK(!model.isDirty()); CHECK(model.machine()->r[0] == 1);
      // puntos de parada y ejecución continua
      ctl.editorChanged("MOV R0,#0\nMOV R1,#1\nMOV R2,#2\nMOV R3,#3\nSWI 0x11\n"); ctl.assemble();
      ctl.toggleBreakpoint(3); CHECK(model.breakpoints().size() == 1);
      { Settings s = model.settings(); s.sim.stepsPerSec = 100000; ctl.updateSettings(s); }
      ctl.toggleRun(); ctl.tick(0.016);
      CHECK(!model.running()); CHECK(model.machine()->r[1] == 1 && model.machine()->r[2] == 0);   // paró antes de la línea 3
      ctl.toggleRun(); ctl.tick(0.016); CHECK(model.machine()->halted && model.machine()->r[3] == 3);
      // edición del estado de la máquina
      ctl.reset(); ctl.setRegister(5, 0xABCD); CHECK(model.machine()->r[5] == 0xABCD);
      ctl.writeMemory(0x3000, 0x11223344, 4); CHECK(model.machine()->readWord(0x3000) == 0x11223344 && model.machine()->readByte(0x3000) == 0x44);
      ctl.toggleFlag('Z'); CHECK(model.machine()->f.Z);
      // ajustes: mapa de memoria personalizado + precarga + registros iniciales
      { Settings s = model.settings(); s.sim.opt.dataBase = 0x8000; s.sim.initRegSet[7] = true; s.sim.initRegs[7] = 42; Preload pl; pl.addr = 0x9000; pl.words = "5, 6"; s.sim.preloads.push_back(pl); ctl.updateSettings(s); }
      ctl.editorChanged(".data\nx: .word 1\n.text\nLDR R0,=x\nLDR R1,[R0]\n"); ctl.assemble();
      CHECK(model.program().symbols.at("x") == 0x8000); CHECK(model.machine()->r[7] == 42); CHECK(model.machine()->readWord(0x9004) == 6);
      // persistencia de ajustes
      // Ruta de temporales multiplataforma: "/tmp/..." a pelo no existe en Windows,
      // y std::ofstream no crea directorios que falten, así que el archivo nunca se
      // llegaba a escribir y la prueba comparaba contra un Settings por defecto.
      std::string tmpSettingsFile = (std::filesystem::temp_directory_path() / "armsim_test_settings.ini").string();
      SettingsStore::save(model.settings(), tmpSettingsFile); Settings loaded; SettingsStore::load(loaded, tmpSettingsFile);
      CHECK(loaded.sim.opt.dataBase == 0x8000); CHECK(loaded.ui.theme == model.settings().ui.theme);
      ctl.applyThemePreset(3); CHECK(model.settings().ui.theme == 3);
      // consola interactiva a traves del controlador
      ctl.editorChanged("SWI 0x30\nMOV R4,R0\nSWI 0x21\nSWI 0x11\n"); ctl.assemble();
      ctl.step(); CHECK(model.waitingForInput());
      ctl.submitInput("17"); CHECK(!model.waitingForInput());        // SWI 0x30 ya se ejecuto: R0 = 17
      ctl.step(); CHECK(model.machine()->r[4] == 17);                // MOV R4,R0
      ctl.step(); CHECK(model.machine()->output == "17");            // SWI 0x21
      ctl.step(); CHECK(model.machine()->haltKind == "exit");        // SWI 0x11
    }
    std::cout << (total - failures) << "/" << total << " comprobaciones correctas\n";
    return failures ? 1 : 0;
}
