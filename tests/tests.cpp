// tests/tests.cpp — pruebas del MODELO y del CONTROLADOR (sin interfaz gráfica).
#include "model/SimModel.h"
#include "model/Examples.h"
#include "controller/SimController.h"
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
