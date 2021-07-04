./src.ps1 | Select-String -NotMatch "CxCli.h" | Select-String -NotMatch "DotNet" | Select-String -NotMatch "/tl-parser/" | ForEach-Object {
  clang-format -verbose -style=file -i $_
}
