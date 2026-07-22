#include "window.hpp"

namespace
{
QString hex2 (u8 v)
{
    return QString ("%1").arg (v, 2, 16, QLatin1Char ('0')).toUpper ();
}
QString hex4 (u16 v)
{
    return QString ("%1").arg (v, 4, 16, QLatin1Char ('0')).toUpper ();
}
} // namespace

MainWindow::MainWindow (QWidget *parent)
    : QMainWindow (parent), cpu_ (std::make_unique< Z80CPU > ()), hasExe_ (false),
      loadOrigin_ (0)
{
    buildUi ();
    buildMenus ();

    runTimer_ = new QTimer (this);
    connect (runTimer_, &QTimer::timeout, this, &MainWindow::onTimerTick);

    refreshRegisters ();
    refreshFlags ();
    refreshMemoryView ();
    refreshStack ();

    setWindowTitle ("Z80 Virtual Machine - Maquina Virtual Z80");
    resize (1280, 860);
}

MainWindow::~MainWindow () = default;

void MainWindow::buildUi ()
{
    auto *central = new QWidget (this);
    auto *mainLayout = new QVBoxLayout (central);

    auto *toolbar = new QHBoxLayout ();
    auto *btnOpen = new QPushButton ("Abrir...", this);
    auto *btnSave = new QPushButton ("Salvar", this);
    auto *btnMacro = new QPushButton ("1. Macro", this);
    auto *btnAsm = new QPushButton ("2. Montar", this);
    auto *btnLink = new QPushButton ("3. Ligar", this);
    auto *btnLoad = new QPushButton ("4. Carregar", this);
    auto *btnRun = new QPushButton ("Executar", this);
    auto *btnStep = new QPushButton ("Passo", this);
    auto *btnStop = new QPushButton ("Parar", this);
    auto *btnReset = new QPushButton ("Reset CPU", this);

    linkModeCombo_ = new QComboBox (this);
    linkModeCombo_->addItem ("Ligador-Relocador (Carregador Absoluto)");
    linkModeCombo_->addItem ("Ligador (Carregador Relocador)");

    toolbar->addWidget (btnOpen);
    toolbar->addWidget (btnSave);
    toolbar->addSpacing (12);
    toolbar->addWidget (btnMacro);
    toolbar->addWidget (btnAsm);
    toolbar->addWidget (new QLabel ("Modo:", this));
    toolbar->addWidget (linkModeCombo_);
    toolbar->addWidget (btnLink);
    toolbar->addWidget (btnLoad);
    toolbar->addSpacing (12);
    toolbar->addWidget (btnRun);
    toolbar->addWidget (btnStep);
    toolbar->addWidget (btnStop);
    toolbar->addWidget (btnReset);
    toolbar->addStretch ();

    mainLayout->addLayout (toolbar);

    auto *splitter = new QSplitter (Qt::Horizontal, this);

    QFont mono = QFontDatabase::systemFont (QFontDatabase::FixedFont);

    auto *leftTabs = new QTabWidget (this);
    sourceEdit_ = new QPlainTextEdit (this);
    sourceEdit_->setFont (mono);
    sourceEdit_->setPlaceholderText (
        "; Escreva ou carregue um programa assembly Z80 aqui\n"
        "; Macros sao expandidas automaticamente antes da montagem\n\n"
        "    ORG 0x0000\nSTART:\n    LD A, 1\n    HALT\n");
        
    expandedEdit_ = new QPlainTextEdit (this);
    expandedEdit_->setFont (mono);
    expandedEdit_->setReadOnly (true);
    leftTabs->addTab (sourceEdit_, "Fonte (.asm)");
    leftTabs->addTab (expandedEdit_, "Pos-Macro (expandido)");

    auto *centerTabs = new QTabWidget (this);
    logEdit_ = new QPlainTextEdit (this);
    logEdit_->setFont (mono);
    logEdit_->setReadOnly (true);
    consoleOutput_ = new QTextEdit (this);
    consoleOutput_->setFont (mono);
    consoleOutput_->setReadOnly (true);
    symbolsTable_ = new QTableWidget (this);
    symbolsTable_->setColumnCount (2);
    symbolsTable_->setHorizontalHeaderLabels ({ "Simbolo", "Endereco" });
    symbolsTable_->horizontalHeader ()->setStretchLastSection (true);

    centerTabs->addTab (consoleOutput_, "Saida (porta 0)");
    centerTabs->addTab (logEdit_, "Log / Erros");
    centerTabs->addTab (symbolsTable_, "Tabela de Simbolos");

    tabs_ = new QTabWidget (this);

    auto *rightPanel = new QWidget (this);
    auto *rightLayout = new QVBoxLayout (rightPanel);

    auto *regGroup = new QGroupBox ("Registradores", this);
    auto *regLayout = new QVBoxLayout (regGroup);
    regTable_ = new QTableWidget (this);
    regTable_->setColumnCount (2);
    regTable_->setHorizontalHeaderLabels ({ "Reg", "Valor" });
    regTable_->horizontalHeader ()->setStretchLastSection (true);
    regTable_->verticalHeader ()->setVisible (false);
    regTable_->setFont (mono);
    regLayout->addWidget (regTable_);
    regGroup->setLayout (regLayout);

    auto *flagsGroup = new QGroupBox ("Flags (F)", this);
    auto *flagsLayout = new QVBoxLayout (flagsGroup);
    flagsTable_ = new QTableWidget (this);
    flagsTable_->setColumnCount (6);
    flagsTable_->setHorizontalHeaderLabels ({ "S", "Z", "H", "P/V", "N", "C" });
    flagsTable_->setRowCount (1);
    flagsTable_->verticalHeader ()->setVisible (false);
    flagsTable_->setFont (mono);
    flagsLayout->addWidget (flagsTable_);
    flagsGroup->setLayout (flagsLayout);

    auto *stackGroup = new QGroupBox ("Pilha (proximos 8 valores a partir de SP)", this);
    auto *stackLayout = new QVBoxLayout (stackGroup);
    stackTable_ = new QTableWidget (this);
    stackTable_->setColumnCount (2);
    stackTable_->setHorizontalHeaderLabels ({ "Endereco", "Valor" });
    stackTable_->horizontalHeader ()->setStretchLastSection (true);
    stackTable_->verticalHeader ()->setVisible (false);
    stackTable_->setFont (mono);
    stackLayout->addWidget (stackTable_);
    stackGroup->setLayout (stackLayout);

    rightLayout->addWidget (regGroup);
    rightLayout->addWidget (flagsGroup);
    rightLayout->addWidget (stackGroup);

    auto *memGroup = new QGroupBox ("Memoria", this);
    auto *memLayout = new QVBoxLayout (memGroup);
    auto *memCtl = new QHBoxLayout ();
    memCtl->addWidget (new QLabel ("Endereco inicial (hex):", this));
    memAddrSpin_ = new QSpinBox (this);
    memAddrSpin_->setRange (0, 0xFFF0);
    memAddrSpin_->setDisplayIntegerBase (16);
    memAddrSpin_->setPrefix ("0x");
    connect (memAddrSpin_, QOverload< int >::of (&QSpinBox::valueChanged), this,
            [this] (int) { refreshMemoryView (); });
    memCtl->addWidget (memAddrSpin_);
    memCtl->addStretch ();
    memLayout->addLayout (memCtl);

    memTable_ = new QTableWidget (this);
    memTable_->setColumnCount (17);
    QStringList memHeaders;
    memHeaders << "Endereco";
    for (int i = 0; i < 16; ++i)
        memHeaders << QString ("%1").arg (i, 1, 16).toUpper ();
    memTable_->setHorizontalHeaderLabels (memHeaders);
    memTable_->verticalHeader ()->setVisible (false);
    memTable_->setFont (mono);
    memLayout->addWidget (memTable_);
    memGroup->setLayout (memLayout);

    auto *leftRightSplit = new QSplitter (Qt::Vertical, this);
    leftRightSplit->addWidget (centerTabs);
    leftRightSplit->addWidget (memGroup);
    leftRightSplit->setStretchFactor (0, 1);
    leftRightSplit->setStretchFactor (1, 1);

    splitter->addWidget (leftTabs);
    splitter->addWidget (leftRightSplit);
    splitter->addWidget (rightPanel);
    splitter->setStretchFactor (0, 3);
    splitter->setStretchFactor (1, 4);
    splitter->setStretchFactor (2, 2);

    mainLayout->addWidget (splitter);
    setCentralWidget (central);

    statusLabel_ = new QLabel ("Pronto.", this);
    statusBar ()->addWidget (statusLabel_);

    connect (btnOpen, &QPushButton::clicked, this, &MainWindow::onOpenFile);
    connect (btnSave, &QPushButton::clicked, this, &MainWindow::onSaveFile);
    connect (btnMacro, &QPushButton::clicked, this, &MainWindow::onRunMacro);
    connect (btnAsm, &QPushButton::clicked, this, &MainWindow::onAssemble);
    connect (btnLink, &QPushButton::clicked, this, &MainWindow::onLink);
    connect (btnLoad, &QPushButton::clicked, this, &MainWindow::onLoad);
    connect (btnRun, &QPushButton::clicked, this, &MainWindow::onRun);
    connect (btnStep, &QPushButton::clicked, this, &MainWindow::onStep);
    connect (btnStop, &QPushButton::clicked, this, &MainWindow::onStop);
    connect (btnReset, &QPushButton::clicked, this, &MainWindow::onReset);
    connect (linkModeCombo_, QOverload< int >::of (&QComboBox::currentIndexChanged),
            this, &MainWindow::onLinkModeChanged);
}

void MainWindow::buildMenus ()
{
    auto *fileMenu = menuBar ()->addMenu ("&Arquivo");
    auto *openAct = fileMenu->addAction ("&Abrir...");
    connect (openAct, &QAction::triggered, this, &MainWindow::onOpenFile);
    auto *saveAct = fileMenu->addAction ("&Salvar");
    connect (saveAct, &QAction::triggered, this, &MainWindow::onSaveFile);
    fileMenu->addSeparator ();
    auto *quitAct = fileMenu->addAction ("Sair");
    connect (quitAct, &QAction::triggered, this, &QWidget::close);

    auto *pipelineMenu = menuBar ()->addMenu ("&Pipeline");
    pipelineMenu->addAction ("Processar Macros", this, &MainWindow::onRunMacro);
    pipelineMenu->addAction ("Montar (Assembler)", this, &MainWindow::onAssemble);
    pipelineMenu->addAction ("Ligar (Linker)", this, &MainWindow::onLink);
    pipelineMenu->addAction ("Carregar (Loader)", this, &MainWindow::onLoad);

    auto *runMenu = menuBar ()->addMenu ("&Executar");
    runMenu->addAction ("Executar", this, &MainWindow::onRun);
    runMenu->addAction ("Passo a Passo", this, &MainWindow::onStep);
    runMenu->addAction ("Parar", this, &MainWindow::onStop);
    runMenu->addAction ("Reiniciar CPU", this, &MainWindow::onReset);

    auto *helpMenu = menuBar ()->addMenu ("A&juda");
    auto *aboutAct = helpMenu->addAction ("Sobre");
    connect (aboutAct, &QAction::triggered, this, [this] () {
        QMessageBox::information (
            this, "Sobre",
            "Maquina Virtual Z80\n\n"
            "Macro-Montador (uma passagem, macros aninhadas)\n"
            "Montador de dois passos\n"
            "Ligador de dois passos (Ligador-Relocador / Ligador+Carregador)\n"
            "Executor / Emulador Z80\n\n"
            "UFPel - CDTec - Programacao de Sistemas\n"
            "Prof. Dr. Anderson Priebe Ferrugem");
    });
}

QString MainWindow::tempPath (const QString &suffix) const
{
    return QDir::tempPath () + "/z80vm_" + suffix;
}

void MainWindow::appendLog (const QString &text)
{
    logEdit_->appendPlainText (text);
}

void MainWindow::appendOutput (const QString &text)
{
    consoleOutput_->moveCursor (QTextCursor::End);
    consoleOutput_->insertPlainText (text);
    consoleOutput_->moveCursor (QTextCursor::End);
}

void MainWindow::onOpenFile ()
{
    QString path = QFileDialog::getOpenFileName (this, "Abrir arquivo assembly", QString (),
                                                  "Assembly Z80 (*.asm *.s);;Todos (*)");
    if (path.isEmpty ())
        return;

    QFile f (path);
    if (!f.open (QIODevice::ReadOnly | QIODevice::Text))
    {
        QMessageBox::warning (this, "Erro", "Nao foi possivel abrir o arquivo.");
        return;
    }
    QTextStream in (&f);
    sourceEdit_->setPlainText (in.readAll ());
    currentFilePath_ = path;
    statusLabel_->setText ("Carregado: " + path);
}

void MainWindow::onSaveFile ()
{
    QString path = currentFilePath_;
    if (path.isEmpty ())
        path = QFileDialog::getSaveFileName (this, "Salvar arquivo assembly", QString (),
                                             "Assembly Z80 (*.asm)");
    if (path.isEmpty ())
        return;

    QFile f (path);
    if (!f.open (QIODevice::WriteOnly | QIODevice::Text))
    {
        QMessageBox::warning (this, "Erro", "Nao foi possivel salvar o arquivo.");
        return;
    }
    QTextStream out (&f);
    out << sourceEdit_->toPlainText ();
    currentFilePath_ = path;
    statusLabel_->setText ("Salvo: " + path);
}

void MainWindow::onRunMacro ()
{
    std::string src = sourceEdit_->toPlainText ().toStdString ();
    MacroProcessor mp;
    try
    {
        std::string expanded = mp.process (src);
        expandedEdit_->setPlainText (QString::fromStdString (expanded));
        appendLog ("[Macro] Processamento de macros concluido com sucesso.");
        statusLabel_->setText ("Macros expandidas.");
    }
    catch (std::exception &e)
    {
        appendLog (QString ("[Macro] ERRO: %1").arg (e.what ()));
        QMessageBox::warning (this, "Erro de Macro", e.what ());
    }
}

bool MainWindow::runPipelineUpTo (const QString &stage)
{
    Q_UNUSED (stage);
    return true;
}

void MainWindow::onAssemble ()
{
    std::string src = sourceEdit_->toPlainText ().toStdString ();

    MacroProcessor mp;
    std::string expanded;
    try
    {
        expanded = mp.process (src);
        expandedEdit_->setPlainText (QString::fromStdString (expanded));
    }
    catch (std::exception &e)
    {
        appendLog (QString ("[Macro] ERRO: %1").arg (e.what ()));
        QMessageBox::warning (this, "Erro de Macro", e.what ());
        return;
    }

    Assembler asmObj;
    try
    {
        std::string fname =
            currentFilePath_.isEmpty () ? "programa.asm" : currentFilePath_.toStdString ();
        ObjectFile obj = asmObj.assemble (expanded, fname);

        objPath_ = tempPath ("out.obj");
        ObjFmt::save (obj, objPath_.toStdString ());

        symbolsTable_->setRowCount (0);
        for (auto &s : obj.symbols)
        {
            if (!s.defined)
                continue;
            int row = symbolsTable_->rowCount ();
            symbolsTable_->insertRow (row);
            symbolsTable_->setItem (row, 0, new QTableWidgetItem (
                                               QString::fromStdString (s.name)));
            symbolsTable_->setItem (row, 1,
                                    new QTableWidgetItem ("0x" + hex4 (s.value)));
        }

        appendLog (QString ("[Montador] Montagem concluida com sucesso: %1")
                      .arg (objPath_));
        statusLabel_->setText ("Montado com sucesso (.obj gerado).");
    }
    catch (Z80Error &e)
    {
        appendLog (QString ("[Montador] ERRO:\n%1").arg (e.what ()));
        QMessageBox::warning (this, "Erro de Montagem", e.what ());
    }
}

void MainWindow::onLinkModeChanged (int index)
{
    Q_UNUSED (index);
}

void MainWindow::onLink ()
{
    if (objPath_.isEmpty () || !QFile::exists (objPath_))
    {
        QMessageBox::information (this, "Aviso",
                                  "Monte o programa antes de ligar (passo 2).");
        return;
    }

    try
    {
        ObjectFile obj = ObjFmt::load (objPath_.toStdString ());

        LinkConfig cfg;
        cfg.loadAddr = 0x0000;
        cfg.mode = linkModeCombo_->currentIndex () == 0 ? LinkMode::Absolute
                                                         : LinkMode::Relocatable;

        Linker linker;
        std::vector< ObjectFile > objs { obj };
        ExeFile exe = linker.link (objs, cfg);

        if (!linker.errors ().empty ())
        {
            QString msg;
            for (auto &e : linker.errors ())
                msg += QString::fromStdString (e) + "\n";
            appendLog ("[Ligador] ERROS:\n" + msg);
            QMessageBox::warning (this, "Erro de Ligacao", msg);
            return;
        }

        exePath_ = tempPath ("out.exe");
        ExeFmt::save (exe, exePath_.toStdString ());

        QString modeStr = cfg.mode == LinkMode::Absolute
                             ? "Ligador-Relocador completo (Carregador Absoluto)"
                             : "Ligador (relocacoes pendentes para o Carregador "
                               "Relocador)";
        appendLog (QString ("[Ligador] Ligacao concluida. Modo: %1\nTamanho: %2 "
                            "bytes. Origem: 0x%3")
                      .arg (modeStr)
                      .arg (exe.data.size ())
                      .arg (hex4 (exe.origin)));
        if (!exe.relocs.empty ())
            appendLog (QString ("[Ligador] %1 relocacao(oes) pendente(s) para o "
                                "carregador.")
                          .arg (exe.relocs.size ()));

        statusLabel_->setText ("Ligado com sucesso (.exe gerado).");
    }
    catch (Z80Error &e)
    {
        appendLog (QString ("[Ligador] ERRO: %1").arg (e.what ()));
        QMessageBox::warning (this, "Erro de Ligacao", e.what ());
    }
}

void MainWindow::onLoad ()
{
    if (exePath_.isEmpty () || !QFile::exists (exePath_))
    {
        QMessageBox::information (this, "Aviso", "Ligue o programa antes de carregar "
                                                  "(passo 3).");
        return;
    }

    try
    {
        ExeFile exe = ExeFmt::load (exePath_.toStdString ());

        if (!exe.relocs.empty ())
        {
            for (auto &r : exe.relocs)
            {
                u16 symVal = (u16)r.addend;
                if (r.type == RelocType::ABS16)
                {
                    if (r.offset + 1 < exe.data.size ())
                    {
                        exe.data[r.offset] = (u8)(symVal & 0xFF);
                        exe.data[r.offset + 1] = (u8)(symVal >> 8);
                    }
                }
                else if (r.type == RelocType::ABS8)
                {
                    if (r.offset < exe.data.size ())
                        exe.data[r.offset] = (u8)(symVal & 0xFF);
                }
                else if (r.type == RelocType::REL8)
                {
                    // o offset aponta para o byte de deslocamento: PC = offset + 1
                    u16 pc = (u16)(exe.origin + r.offset + 1);
                    i8 rel = (i8)((i16)symVal - (i16)pc);
                    if (r.offset < exe.data.size ())
                        exe.data[r.offset] = (u8)rel;
                }
            }
            appendLog (QString ("[Carregador] Carregador Relocador: %1 relocacao(oes) "
                                "aplicada(s) em 0x%2.")
                          .arg (exe.relocs.size ())
                          .arg (hex4 (exe.origin)));
        }

        cpu_->reset ();
        cpu_->loadBinary (exe.data, exe.origin);
        loadOrigin_ = exe.origin;
        hasExe_ = true;

        cpu_->ioWrite = [this] (u16 port, u8 val) {
            if ((port & 0xFF) == 0x00)
                appendOutput (QString (QChar ((char)val)));
        };
        cpu_->ioRead = [] (u16) -> u8 { return 0xFF; };

        refreshRegisters ();
        refreshFlags ();
        refreshMemoryView ();
        refreshStack ();

        appendLog (QString ("[Carregador] Programa carregado em 0x%1 (%2 bytes).")
                      .arg (hex4 (exe.origin))
                      .arg (exe.data.size ()));
        statusLabel_->setText ("Programa carregado. Pronto para executar.");
    }
    catch (Z80Error &e)
    {
        appendLog (QString ("[Carregador] ERRO: %1").arg (e.what ()));
        QMessageBox::warning (this, "Erro de Carga", e.what ());
    }
}

void MainWindow::onRun ()
{
    if (!hasExe_)
    {
        QMessageBox::information (this, "Aviso", "Carregue um programa antes de "
                                                  "executar (passo 4).");
        return;
    }
    if (cpu_->regs.halted)
    {
        appendLog ("[Executor] CPU ja esta parada (HALT). Use Reset para reiniciar.");
        return;
    }
    runTimer_->start (15);
    statusLabel_->setText ("Executando...");
}

void MainWindow::onStep ()
{
    if (!hasExe_)
    {
        QMessageBox::information (this, "Aviso", "Carregue um programa antes de "
                                                  "executar (passo 4).");
        return;
    }
    if (!cpu_->regs.halted)
        cpu_->step ();
    refreshRegisters ();
    refreshFlags ();
    refreshMemoryView ();
    refreshStack ();
    if (cpu_->regs.halted)
    {
        appendLog ("[Executor] CPU parada (HALT).");
        statusLabel_->setText ("CPU parada (HALT).");
    }
}

void MainWindow::onStop ()
{
    runTimer_->stop ();
    statusLabel_->setText ("Execucao interrompida.");
}

void MainWindow::onReset ()
{
    runTimer_->stop ();
    cpu_->reset ();
    if (hasExe_)
        cpu_->regs.PC = loadOrigin_;
    refreshRegisters ();
    refreshFlags ();
    refreshMemoryView ();
    refreshStack ();
    statusLabel_->setText ("CPU reiniciada.");
}

void MainWindow::onTimerTick ()
{
    if (!hasExe_ || cpu_->regs.halted)
    {
        runTimer_->stop ();
        refreshRegisters ();
        refreshFlags ();
        refreshMemoryView ();
        refreshStack ();
        if (cpu_->regs.halted)
        {
            appendLog ("[Executor] CPU parada (HALT).");
            statusLabel_->setText ("CPU parada (HALT).");
        }
        return;
    }

    for (int i = 0; i < 200 && !cpu_->regs.halted; ++i)
        cpu_->step ();

    refreshRegisters ();
    refreshFlags ();
    refreshMemoryView ();
    refreshStack ();
}

void MainWindow::refreshRegisters ()
{
    auto &r = cpu_->regs;
    struct RegRow
    {
        QString name;
        QString value;
    };
    std::vector< RegRow > rows = {
        { "A", hex2 (r.A) },         { "F", hex2 (r.F) },
        { "B", hex2 (r.B) },         { "C", hex2 (r.C) },
        { "D", hex2 (r.D) },         { "E", hex2 (r.E) },
        { "H", hex2 (r.H) },         { "L", hex2 (r.L) },
        { "AF", hex4 ((u16)(r.A << 8 | r.F)) },
        { "BC", hex4 ((u16)(r.B << 8 | r.C)) },
        { "DE", hex4 ((u16)(r.D << 8 | r.E)) },
        { "HL", hex4 ((u16)(r.H << 8 | r.L)) },
        { "PC", hex4 (r.PC) },       { "SP", hex4 (r.SP) },
        { "IX", hex4 (r.IX) },       { "IY", hex4 (r.IY) },
        { "I", hex2 (r.I) },         { "R", hex2 (r.R) },
        { "IFF1", r.IFF1 ? "1" : "0" }, { "IFF2", r.IFF2 ? "1" : "0" },
        { "HALT", r.halted ? "SIM" : "nao" },
    };

    regTable_->setRowCount ((int)rows.size ());
    for (int i = 0; i < (int)rows.size (); ++i)
    {
        regTable_->setItem (i, 0, new QTableWidgetItem (rows[i].name));
        regTable_->setItem (i, 1, new QTableWidgetItem (rows[i].value));
    }
}

void MainWindow::refreshFlags ()
{
    u8 f = cpu_->regs.F;
    QStringList vals = {
        (f & 0x80) ? "1" : "0", (f & 0x40) ? "1" : "0", (f & 0x10) ? "1" : "0",
        (f & 0x04) ? "1" : "0", (f & 0x02) ? "1" : "0", (f & 0x01) ? "1" : "0",
    };
    for (int i = 0; i < 6; ++i)
        flagsTable_->setItem (0, i, new QTableWidgetItem (vals[i]));
}

void MainWindow::refreshMemoryView ()
{
    u16 base = (u16)(memAddrSpin_->value () & 0xFFF0);
    memTable_->setRowCount (16);
    for (int row = 0; row < 16; ++row)
    {
        u16 rowAddr = (u16)(base + row * 16);
        memTable_->setItem (row, 0, new QTableWidgetItem ("0x" + hex4 (rowAddr)));
        for (int col = 0; col < 16; ++col)
        {
            u16 addr = (u16)(rowAddr + col);
            u8 val = cpu_->mem[addr];
            auto *item = new QTableWidgetItem (hex2 (val));
            if (addr == cpu_->regs.PC)
                item->setBackground (QColor (255, 230, 150));
            else if (addr == cpu_->regs.SP)
                item->setBackground (QColor (180, 220, 255));
            memTable_->setItem (row, col + 1, item);
        }
    }
}

void MainWindow::refreshStack ()
{
    stackTable_->setRowCount (8);
    u16 sp = cpu_->regs.SP;
    for (int i = 0; i < 8; ++i)
    {
        u16 addr = (u16)(sp + i);
        stackTable_->setItem (i, 0, new QTableWidgetItem ("0x" + hex4 (addr)));
        stackTable_->setItem (i, 1, new QTableWidgetItem (hex2 (cpu_->mem[addr])));
    }
}