include(../../plugins/coreplugin/coreplugin.pri)
include(../../libs/utils/utils.pri)
LIBS *= -l$$qtLibraryName(TextEditor)
LIBS *= -l$$qtLibraryName(ProjectExplorer)
