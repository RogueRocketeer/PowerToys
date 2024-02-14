﻿// Copyright (c) Microsoft Corporation
// The Microsoft Corporation licenses this file to you under the MIT license.
// See the LICENSE file in the project root for more information.

using FileActionsMenu.Ui.Helpers;
using Wpf.Ui.Controls;

namespace FileActionsMenu.Ui.Actions.Hashes
{
    internal sealed class SingleFile(Hashes.Hashes.HashCallingAction hashCallingAction) : ICheckableAction
    {
        private readonly Hashes.Hashes.HashCallingAction _hashCallingAction = hashCallingAction;
        private string[]? _selectedItems;

        public override string[] SelectedItems { get => _selectedItems.GetOrArgumentNullException(); set => _selectedItems = value; }

        public override string Header => _hashCallingAction == Hashes.Hashes.HashCallingAction.GENERATE ? "Save hashes in single file" : "Compare with hashes in file called \"Hashes\"";

        public override IconElement? Icon => null;

        public override bool IsVisible => true;

        private bool _isChecked;

        public override bool IsChecked { get => _isChecked; set => _isChecked = value; }

        public override bool IsCheckedByDefault => true;

        public override string? CheckableGroupUUID => Hashes.Hashes.GetUUID(_hashCallingAction);
    }
}