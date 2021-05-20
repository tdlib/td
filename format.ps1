./src.ps1 | Select-String -NotMatch "CxCli.h" | Select-String -NotMatch "DotNet" | ForEach-Object {
  clang-format -verbose -style=file -i $_
}
