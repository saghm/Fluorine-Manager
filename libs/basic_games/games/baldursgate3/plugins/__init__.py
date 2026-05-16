import mobase

from .convert_jsons_to_yaml_plugin import BG3ToolConvertJsonsToYaml
from .reparse_pak_metadata_plugin import BG3ToolReparsePakMetadata


def createPlugins() -> list[mobase.IPluginTool]:
    return [
        BG3ToolReparsePakMetadata(),
        BG3ToolConvertJsonsToYaml(),
    ]
